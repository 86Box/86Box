/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Ensoniq AudioPCI family emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          RichardG, <richardg867@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          Cacodemon345
 *
 *          Copyright 2008-2021 Sarah Walker.
 *          Copyright 2021-2024 RichardG.
 *          Copyright 2021      Miran Grca.
 *          Copyright 2024-2025 Jasmine Iwanek.
 *          Copyright 2024-2025 Cacodemon345.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/nmi.h>
#include <86box/pci.h>
#include <86box/snd_ac97.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>
#include <86box/snd_akm4531.h>

#define N            16

#define ES137x_NCoef 91

static float low_fir_es137x_coef[ES137x_NCoef];

typedef struct es137x_t {
    uint8_t pci_command;
    uint8_t pci_serr;

    uint8_t subsys_lock;
    uint8_t subsys_id[4];

    uint32_t base_addr;

    uint8_t int_line;
    uint8_t irq_state;

    uint16_t pmcsr;

    uint32_t int_ctrl;
    uint32_t int_status;
    uint32_t legacy_ctrl;
    uint32_t spdif_chstatus;
    void    *gameport;

    int mem_page;

    uint32_t si_cr;

    uint32_t sr_cir;
    uint16_t sr_ram[128];

    uint8_t  uart_data;
    uint8_t  uart_ctrl;
    uint8_t  uart_status;
    uint8_t  uart_res;
    uint32_t uart_fifo[8];
    uint8_t  read_fifo_pos;
    uint8_t  write_fifo_pos;

    ac97_codec_t *codec;
    uint32_t      codec_ctrl;

    struct {
        uint32_t addr;
        uint32_t addr_latch;
        uint16_t count;
        uint16_t size;

        uint16_t samp_ct;
        int      curr_samp_ct;

        pc_timer_t timer;
        uint64_t   latch;

        uint32_t vf;
        uint32_t ac;

        int16_t buffer_l[64];
        int16_t buffer_r[64];
        int     buffer_pos;
        int     buffer_pos_end;

        int filtered_l[32];
        int filtered_r[32];
        int f_pos;

        int16_t out_l;
        int16_t out_r;

        int16_t prev_out_l;
        int16_t prev_out_r;

        int32_t vol_l;
        int32_t vol_r;
    } dac[2], adc;

    int64_t dac_latch;
    int64_t dac_time;

    int master_vol_l;
    int master_vol_r;
    int pcm_vol_l;
    int pcm_vol_r;
    int pcm_rear_vol_l;
    int pcm_rear_vol_r;
    int cd_vol_l;
    int cd_vol_r;

    uint8_t pci_slot;

    int     pos;
    int16_t buffer[WTBUFLEN * 2];

    uint32_t type;

    akm4531_t akm_codec;

    uint32_t calc_sample_rate;
    uint32_t calc_sample_rate_synth;

    double   interp_factor;
    uint32_t interp_step;

    double   interp_factor_synth;
    uint32_t interp_step_synth;

    uint32_t step_pcm;
    uint32_t step_synth;
} es137x_t;

static const double akm4531_att_2dbstep_5bits[] = {
    // clang-format off
       25.0,    32.0,    41.0,    51.0,    65.0,    82.0,   103.0,   130.0,
      164.0,   206.0,   260.0,   327.0,   412.0,   519.0,   653.0,   822.0,
     1036.0,  1304.0,  1641.0,  2067.0,  2602.0,  3276.0,  4125.0,  5192.0,
     6537.0,  8230.0, 10362.0, 13044.0, 16422.0, 20674.0, 26027.0, 32767.0
    // clang-format on
};

static double akm4531_gain_2dbstep_5bits[0x20];

#define AUDIOPCI_ES1370           0x50000000
#define AUDIOPCI_ES1371           0x13710200
#define AUDIOPCI_ES1373           0x13710400
#define AUDIOPCI_CT5880           0x58800400

#define LEGACY_SB_ADDR            (1 << 29)
#define LEGACY_SSCAPE_ADDR_SHIFT  27
#define LEGACY_CODEC_ADDR_SHIFT   25
#define LEGACY_FORCE_IRQ          (1 << 24)
#define LEGACY_CAPTURE_SLAVE_DMA  (1 << 23)
#define LEGACY_CAPTURE_SLAVE_PIC  (1 << 22)
#define LEGACY_CAPTURE_MASTER_DMA (1 << 21)
#define LEGACY_CAPTURE_MASTER_PIC (1 << 20)
#define LEGACY_CAPTURE_ADLIB      (1 << 19)
#define LEGACY_CAPTURE_SB         (1 << 18)
#define LEGACY_CAPTURE_CODEC      (1 << 17)
#define LEGACY_CAPTURE_SSCAPE     (1 << 16)
#define LEGACY_EVENT_SSCAPE       (0 << 8)
#define LEGACY_EVENT_CODEC        (1 << 8)
#define LEGACY_EVENT_SB           (2 << 8)
#define LEGACY_EVENT_ADLIB        (3 << 8)
#define LEGACY_EVENT_MASTER_PIC   (4 << 8)
#define LEGACY_EVENT_MASTER_DMA   (5 << 8)
#define LEGACY_EVENT_SLAVE_PIC    (6 << 8)
#define LEGACY_EVENT_SLAVE_DMA    (7 << 8)
#define LEGACY_EVENT_MASK         (7 << 8)
#define LEGACY_EVENT_ADDR_SHIFT   3
#define LEGACY_EVENT_ADDR_MASK    (0x1f << 3)
#define LEGACY_EVENT_TYPE_RW      (1 << 2)
#define LEGACY_INT                (1 << 0)

#define SRC_RAM_WE                (1 << 24)

#define CODEC_READ                (1 << 23)
#define CODEC_READY               (1 << 31)

#define INT_DAC1_BYPASS           (1 << 31)
#define INT_DAC2_BYPASS           (1 << 30)
#define INT_DAC1_EN               (1 << 6)
#define INT_DAC2_EN               (1 << 5)
#define INT_UART_EN               (1 << 3)

#define SI_P2_PAUSE               (1 << 12)
#define SI_P1_PAUSE               (1 << 11)
#define SI_P2_INTR_EN             (1 << 9)
#define SI_P1_INTR_EN             (1 << 8)

#define INT_STATUS_INTR           (1 << 31)
#define INT_STATUS_REAR_B27       (1 << 27)
#define INT_STATUS_REAR_B26       (1 << 26)
#define INT_STATUS_REAR_B24       (1 << 24)
#define INT_STATUS_UART           (1 << 3)
#define INT_STATUS_DAC1           (1 << 2)
#define INT_STATUS_DAC2           (1 << 1)

#define UART_CTRL_RXINTEN         (1 << 7)
#define UART_CTRL_TXINTEN         (3 << 5)

#define UART_STATUS_RXINT         (1 << 7)
#define UART_STATUS_TXINT         (1 << 2)
#define UART_STATUS_TXRDY         (1 << 1)
#define UART_STATUS_RXRDY         (1 << 0)

#define UART_FIFO_BYTE_VALID      0x00000100

#define FORMAT_MONO_8             0
#define FORMAT_STEREO_8           1
#define FORMAT_MONO_16            2
#define FORMAT_STEREO_16          3

static void es137x_fetch(es137x_t *dev, int dac_nr);
static void update_legacy(es137x_t *dev, uint32_t old_legacy_ctrl);

#ifdef ENABLE_AUDIOPCI_LOG
int audiopci_do_log = ENABLE_AUDIOPCI_LOG;

static void
audiopci_log(const char *fmt, ...)
{
    va_list ap;

    if (audiopci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define audiopci_log(fmt, ...)
#endif

static void
es137x_update_irqs(es137x_t *dev)
{
    int irq = 0;

    if ((dev->int_status & INT_STATUS_DAC1) && (dev->si_cr & SI_P1_INTR_EN))
        irq = 1;
    if ((dev->int_status & INT_STATUS_DAC2) && (dev->si_cr & SI_P2_INTR_EN))
        irq = 1;

    dev->int_status &= ~INT_STATUS_UART;

    if ((dev->uart_status & UART_STATUS_TXINT) || (dev->uart_status & UART_STATUS_RXINT)) {
        dev->int_status |= INT_STATUS_UART;
        irq = 1;
    }

    if (irq)
        dev->int_status |= INT_STATUS_INTR;
    else
        dev->int_status &= ~INT_STATUS_INTR;

    if (dev->legacy_ctrl & LEGACY_FORCE_IRQ)
        irq = 1;

    if (irq)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

static void
es137x_update_tx_irq(es137x_t *dev)
{
    dev->uart_status &= ~UART_STATUS_TXINT;

    if (((dev->uart_ctrl & UART_CTRL_TXINTEN) == 0x20) && (dev->uart_status & UART_STATUS_TXRDY))
        dev->uart_status |= UART_STATUS_TXINT;

    es137x_update_irqs(dev);
}

static void
es137x_set_tx_irq(es137x_t *dev, int set)
{
    dev->uart_status &= ~UART_STATUS_TXRDY;

    if (set)
        dev->uart_status |= UART_STATUS_TXRDY;

    es137x_update_tx_irq(dev);
}

static void
es137x_update_rx_irq(es137x_t *dev)
{
    dev->uart_status &= ~UART_STATUS_RXINT;

    if ((dev->uart_ctrl & UART_CTRL_RXINTEN) && (dev->uart_status & UART_STATUS_RXRDY))
        dev->uart_status |= UART_STATUS_RXINT;

    es137x_update_irqs(dev);
}

static void
es137x_set_rx_irq(es137x_t *dev, int set)
{
    dev->uart_status &= ~UART_STATUS_RXRDY;

    if (set)
        dev->uart_status |= UART_STATUS_RXRDY;

    es137x_update_rx_irq(dev);
}

static void
es137x_scan_fifo(es137x_t *dev)
{
    if (dev->read_fifo_pos != dev->write_fifo_pos) {
        dev->uart_data     = dev->uart_fifo[dev->read_fifo_pos];
        dev->read_fifo_pos = (dev->read_fifo_pos + 1) & 7;

        es137x_set_rx_irq(dev, 1);
    } else
        es137x_set_rx_irq(dev, 0);
}

static void
es137x_write_fifo(es137x_t *dev, uint8_t val)
{
    if (dev->write_fifo_pos < 8) {
        dev->uart_fifo[dev->write_fifo_pos] = val | UART_FIFO_BYTE_VALID;
        dev->write_fifo_pos                 = (dev->write_fifo_pos + 1) & 7;
    }
}

static void
es137x_reset_fifo(es137x_t *dev)
{
    for (uint8_t i = 0; i < 8; i++)
        dev->uart_fifo[i] = 0x00000000;

    dev->read_fifo_pos = dev->write_fifo_pos = 0;

    es137x_set_rx_irq(dev, 0);
}

static void
akm4531_reset(es137x_t *dev)
{
    akm4531_t *codec = &dev->akm_codec;

    memset(codec->registers, 0, sizeof(codec->registers));

    codec->registers[0] = 0x80;
    codec->registers[1] = 0x80;

    for (int i = 0x02; i <= 0x0E; i++) {
        codec->registers[i] = 0b10000110;
    }

    codec->registers[0xf] = 0x80;

    codec->registers[0x17] = 0x3;
    codec->registers[0x16] = 0x3;
}

static double
lerp(double v0, double v1, double t)
{
    return (1. - t) * v0 + t * v1;
}

static void
es1370_calc_sample_rate(es137x_t *dev)
{
    if (dev->type != AUDIOPCI_ES1370)
        return;

    dev->calc_sample_rate = 1411200 / (((dev->int_ctrl >> 16) & 0x1fff) + 2);

    // audiopci_log("ES1370 calc sample rate %u\n", dev->calc_sample_rate);

    dev->interp_factor = 1.0;
    dev->interp_step   = 1;

    if (dev->calc_sample_rate >= 44100 || dev->calc_sample_rate < 11025) {
        dev->interp_factor    = 1.0;
        dev->interp_step      = 1;
        dev->calc_sample_rate = 44100;
    }
    if (dev->calc_sample_rate == 22050) {
        dev->interp_factor = 0.5;
        dev->interp_step   = 2;
    }
    if (dev->calc_sample_rate == 11025) {
        dev->interp_factor = 0.25;
        dev->interp_step   = 4;
    }
    if ((((dev->int_ctrl >> 16) & 0x1fff) + 2) == 256) {
        /* 5512.5 Hz */
        dev->interp_factor    = 0.125;
        dev->interp_step      = 8;
        dev->calc_sample_rate = 5512;
    }

    dev->calc_sample_rate_synth = 44100 / (1 << (((dev->int_ctrl >> 12) & 3) ^ 3));
    dev->interp_factor_synth    = 1. / (double) ((1 << ((dev->int_ctrl >> 12) & 3) ^ 3));
    dev->interp_step_synth      = (1 << (((dev->int_ctrl >> 12) & 3) ^ 3));
}

static void
es137x_reset(void *priv)
{
    es137x_t *dev = (es137x_t *) priv;

    nmi = 0;

    /* Default subsystem ID. */
    dev->subsys_lock                   = 0x00;
    *((uint16_t *) &dev->subsys_id[0]) = (dev->type == AUDIOPCI_ES1370) ? 0x4942 : 0x1274;
    *((uint16_t *) &dev->subsys_id[2]) = (dev->type == AUDIOPCI_ES1370) ? 0x4c4c : 0x1371;

    /* Interrupt/Chip Select Control Register, Address 00H
       Addressable as byte, word, longword */
    dev->int_ctrl = 0xfcff0000;

    /* Interrupt/Chip Select Status Register, Address 04H
       Addressable as longword only */
    if (dev->type == AUDIOPCI_ES1370)
        dev->int_status = 0x00000060;
    else if (dev->type == AUDIOPCI_CT5880)
        dev->int_status = 0x52080ec0;
    else if (dev->type == AUDIOPCI_ES1373)
        dev->int_status = 0x7f080ec0;
    else /* AUDIOPCI_ES1371 */
        dev->int_status = 0x7ffffec0;

    /* UART Status Register, Address 09H
       Addressable as byte only */
    dev->uart_status = 0xff;

    /* UART Control Register, Address 09H
       Addressable as byte only */
    dev->uart_ctrl = 0x00;

    /* UART Reserved Register, Address 0AH
       Addressable as byte only */
    dev->uart_res = 0xff;

    /* Memory Page Register, Address 0CH
       Addressable as byte, word, longword */
    dev->mem_page = 0xf0; /* FIXME: hardware reads 0xfffffff0 */

    /* Sample Rate Converter Interface Register, Address 10H
       Addressable as longword only */
    dev->sr_cir = 0x00470000;

    /* CODEC Write Register, Address 14H
       Addressable as longword only */
    dev->codec_ctrl = 0x00000000;

    /* Legacy Control/Status Register, Address 18H
       Addressable as byte, word, longword */
    dev->legacy_ctrl = 0x0000f801;

    /* S/PDIF Channel Status Control Register, Address 1CH
       Addressable as byte, word, longword */
    dev->spdif_chstatus = 0xc0200004;

    /* Serial Interface Control Register, Address 20H
       Addressable as byte, word, longword */
    dev->si_cr = 0xff800000;

    /* DAC1 Channel Sample Count Register, Address 24H
       Addressable as word, longword */
    dev->dac[0].samp_ct      = 0x00000000; /* FIXME: hardware reads 0x00010000 */
    dev->dac[0].curr_samp_ct = 0x00000000;

    /* DAC2 Channel Sample Count Register, Address 28H
       Addressable as word, longword */
    dev->dac[1].samp_ct      = 0x00000000; /* FIXME: hardware reads 0x00010000 */
    dev->dac[1].curr_samp_ct = 0x00000000;

    /* ADC Channel Sample Count Register, Address 2CH
       Addressable as word, longword */
    dev->adc.samp_ct      = 0x00000000; /* FIXME: hardware reads 0x00010000 */
    dev->adc.curr_samp_ct = 0x00000000;

    /* DAC1 Frame Register 1, Address 30H, Memory Page 1100b
       Addressable as longword only */
    dev->dac[0].addr_latch = 0x00000000;

    /* DAC1 Frame Register 2, Address 34H, Memory Page 1100b
       Addressable as longword only */
    dev->dac[0].size  = 0x00000000;
    dev->dac[0].count = 0x00000000;

    /* DAC2 Frame Register 1, Address 38H, Memory Page 1100b
       Addressable as longword only */
    dev->dac[1].addr_latch = 0x00000000;

    /* DAC2 Frame Register 2, Address 3CH, Memory Page 1100b
       Addressable as longword only */
    dev->dac[1].size  = 0x00000000;
    dev->dac[1].count = 0x00000000;

    /* ADC Frame Register 1, Address 30H, Memory Page 1101b
       Addressable as longword only */
    dev->adc.addr_latch = 0x00000000;

    /* ADC Frame Register 2, Address 34H, Memory Page 1101b
       Addressable as longword only */
    dev->adc.size  = 0x00000000;
    dev->adc.count = 0x00000000;

    /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
       Addressable as longword only */
    for (uint8_t i = 0; i < 8; i++)
        dev->uart_fifo[i] = 0xffff0000;

    /* Reset the UART TX. */
    es137x_set_tx_irq(dev, 0);

    /* Reset the UART (RX) FIFO. */
    es137x_reset_fifo(dev);

    /* Update interrupts to ensure they're all correctly cleared. */
    es137x_update_irqs(dev);

    /* Reset the codec. */
    akm4531_reset(dev);
}

static uint32_t
es137x_read_frame_reg(es137x_t *dev, int frame, int page)
{
    uint32_t ret = 0xffffffff;

    switch (frame) {
        case 0x30:
            switch (page) {
                /* DAC1 Frame Register 1, Address 30H, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    ret = dev->dac[0].addr_latch;
                    break;
                /* ADC Frame Register 1, Address 30H, Memory Page 1101b
                   Addressable as longword only */
                case 0xd:
                    ret = dev->adc.addr_latch;
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[30:%02X] ret = dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03),
                                 dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)]);
                    ret = dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)];
                    break;

                default:
                    break;
            }
            break;
        case 0x34:
            switch (page) {
                /* DAC1 Frame Register 2, Address 34H, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    ret = dev->dac[0].size | (dev->dac[0].count << 16);
                    break;
                /* ADC Frame Register 2, Address 34H, Memory Page 1101b
                   Addressable as longword only */
                case 0xd:
                    ret = dev->adc.size | (dev->adc.count << 16);
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[34:%02X] ret = dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03),
                                 dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)]);
                    ret = dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)];
                    break;

                default:
                    break;
            }
            break;
        case 0x38:
            switch (page) {
                /* DAC2 Frame Register 1, Address 38H, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    ret = dev->dac[1].addr_latch;
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[38:%02X] ret = dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03),
                                 dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)]);
                    ret = dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)];
                    break;

                default:
                    break;
            }
            break;
        case 0x3c:
            switch (page) {
                /* DAC2 Frame Register 2, Address 3CH, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    ret = dev->dac[1].size | (dev->dac[1].count << 16);
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[3C:%02X] ret = dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03),
                                 dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)]);
                    ret = dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)];
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return ret;
}

static void
es137x_write_frame_reg(es137x_t *dev, int frame, int page, uint32_t val)
{
    switch (frame) {
        case 0x30:
            switch (page) {
                /* DAC1 Frame Register 1, Address 30H, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    dev->dac[0].addr_latch = val;
                    break;
                /* ADC Frame Register 1, Address 30H, Memory Page 1101b
                   Addressable as longword only */
                case 0xd:
                    dev->adc.addr_latch = val;
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[30:%02X] dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03), val);
                    dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)] = val;
                    break;

                default:
                    break;
            }
            break;
        case 0x34:
            switch (page) {
                /* DAC1 Frame Register 2, Address 34H, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    dev->dac[0].size  = val & 0xffff;
                    dev->dac[0].count = val >> 16;
                    break;
                /* ADC Frame Register 2, Address 34H, Memory Page 1101b
                   Addressable as longword only */
                case 0xd:
                    dev->adc.size  = val & 0xffff;
                    dev->adc.count = val >> 16;
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[34:%02X] dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03), val);
                    dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)] = val;
                    break;

                default:
                    break;
            }
            break;
        case 0x38:
            switch (page) {
                /* DAC2 Frame Register 1, Address 38H, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    dev->dac[1].addr_latch = val;
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[38:%02X] dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03), val);
                    dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)] = val;
                    break;

                default:
                    break;
            }
            break;
        case 0x3c:
            switch (page) {
                /* DAC2 Frame Register 2, Address 3CH, Memory Page 1100b
                   Addressable as longword only */
                case 0xc:
                    dev->dac[1].size  = val & 0xffff;
                    dev->dac[1].count = val >> 16;
                    break;
                /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
                   Addressable as longword only */
                case 0xe:
                case 0xf:
                    audiopci_log("[3C:%02X] dev->uart_fifo[%02X] = %08X\n", page,
                                 ((page & 0x01) << 2) + ((frame >> 2) & 0x03), val);
                    dev->uart_fifo[((page & 0x01) << 2) + ((frame >> 2) & 0x03)] = val;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    if (page == 0x0e || page == 0x0f) {
        audiopci_log("Write frame = %02x, page = %02x, uart fifo = %08x, val = %02x\n", frame, page, dev->uart_fifo, val);
    }
}

static uint8_t
es137x_inb(uint16_t port, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    uint8_t   ret = 0xff;

    switch (port & 0x3f) {
        /* Interrupt/Chip Select Control Register, Address 00H
           Addressable as byte, word, longword */
        case 0x00:
            ret = dev->int_ctrl & 0xff;
            break;
        case 0x01:
            ret = (dev->int_ctrl >> 8) & 0xff;
            break;
        case 0x02:
            ret = (dev->int_ctrl >> 16) & 0x0f;
            break;
        case 0x03:
            ret = dev->int_ctrl >> 24;
            if (dev->type == AUDIOPCI_ES1371)
                ret |= 0xfc;
            break;

        /* Interrupt/Chip Select Status Register, Address 04H
           Addressable as longword only, but PCem implements byte access, which
           must be for a reason */
        case 0x04:
            ret = dev->int_status & 0xff;
            audiopci_log("[R] STATUS  0- 7 = %02X\n", ret);
            break;
        case 0x05:
            ret = (dev->int_status >> 8) & 0xff;
            audiopci_log("[R] STATUS  8-15 = %02X\n", ret);
            break;
        case 0x06:
            ret = (dev->int_status >> 16) & 0xff;
            audiopci_log("[R] STATUS 16-23 = %02X\n", ret);
            break;
        case 0x07:
            ret = (dev->int_status >> 24) & 0xff;
            audiopci_log("[R] STATUS 24-31 = %02X\n", ret);
            break;

        /* UART Data Register, Address 08H
           Addressable as byte only */
        case 0x08:
            ret = dev->uart_data;
            es137x_set_rx_irq(dev, 0);
            audiopci_log("[R] UART DATA = %02X\n", ret);
            break;

        /* UART Status Register, Address 09H
           Addressable as byte only */
        case 0x09:
            ret = dev->uart_status & 0x87;
            audiopci_log("ES1371 UART Status = %02x\n", dev->uart_status);
            break;

        /* UART Reserved Register, Address 0AH
           Addressable as byte only */
        case 0x0a:
            ret = dev->uart_res & 0x01;
            audiopci_log("[R] UART RES = %02X\n", ret);
            break;

        /* Memory Page Register, Address 0CH
           Addressable as byte, word, longword */
        case 0x0c:
            ret = dev->mem_page;
            break;
        case 0x0d ... 0x0e:
            ret = 0x00;
            break;

        /* Legacy Control/Status Register, Address 18H
           Addressable as byte, word, longword */
        case 0x18:
            ret = dev->legacy_ctrl & 0xfd;
            break;
        case 0x19:
            ret = ((dev->legacy_ctrl >> 8) & 0x07) | 0xf8;
            break;
        case 0x1a:
            ret = dev->legacy_ctrl >> 16;
            break;
        case 0x1b:
            ret = dev->legacy_ctrl >> 24;
            break;

        /* S/PDIF Channel Status Control Register, Address 1CH
           Addressable as byte, word, longword */
        case 0x1c:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus & 0xff;
            break;
        case 0x1d:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus >> 8;
            break;
        case 0x1e:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus >> 16;
            break;
        case 0x1f:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus >> 24;
            break;

        /* Serial Interface Control Register, Address 20H
            Addressable as byte, word, longword */
        case 0x20:
            ret = dev->si_cr & 0xff;
            break;
        case 0x21:
            ret = dev->si_cr >> 8;
            break;
        case 0x22:
            ret = dev->si_cr >> 16;
            if (dev->type != AUDIOPCI_ES1370)
                ret |= 0x80;
            break;
        case 0x23:
            if (dev->type == AUDIOPCI_ES1370)
                ret = 0x00;
            else
                ret = 0xff;
            break;

        default:
            audiopci_log("Bad es137x_inb: port=%04x\n", port);
    }

    audiopci_log("es137x_inb: port=%04x ret=%02x\n", port, ret);
    return ret;
}

static uint16_t
es137x_inw(uint16_t port, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    uint16_t  ret = 0xffff;

    switch (port & 0x3e) {
        /* Interrupt/Chip Select Control Register, Address 00H
           Addressable as byte, word, longword */
        case 0x00:
            ret = dev->int_ctrl & 0xffff;
            break;
        case 0x02:
            ret = (dev->int_ctrl >> 16) & 0xff0f;
            if (dev->type == AUDIOPCI_ES1371)
                ret |= 0xfc00;
            break;

        /* Memory Page Register, Address 0CH
           Addressable as byte, word, longword */
        case 0x0c:
            ret = dev->mem_page;
            break;
        case 0x0e:
            ret = 0x0000;
            break;

        /* Legacy Control/Status Register, Address 18H
           Addressable as byte, word, longword */
        case 0x18:
            ret = (dev->legacy_ctrl & 0x07fd) | 0xf800;
            break;
        case 0x1a:
            ret = dev->legacy_ctrl >> 16;
            break;

        /* S/PDIF Channel Status Control Register, Address 1CH
           Addressable as byte, word, longword */
        case 0x1c:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus & 0xffff;
            break;
        case 0x1e:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus >> 16;
            break;

        /* Serial Interface Control Register, Address 20H
            Addressable as byte, word, longword */
        case 0x20:
            ret = dev->si_cr & 0xffff;
            break;
        case 0x22:
            ret = dev->si_cr >> 16;
            if (dev->type != AUDIOPCI_ES1370)
                ret |= 0xff80;
            break;

        /* DAC1 Channel Sample Count Register, Address 24H
           Addressable as word, longword */
        case 0x24:
            ret = dev->dac[0].samp_ct;
            break;
        case 0x26:
            ret = dev->dac[0].curr_samp_ct;
            break;

        /* DAC2 Channel Sample Count Register, Address 28H
           Addressable as word, longword */
        case 0x28:
            ret = dev->dac[1].samp_ct;
            break;
        case 0x2a:
            ret = dev->dac[1].curr_samp_ct;
            break;

        /* ADC Channel Sample Count Register, Address 2CH
           Addressable as word, longword */
        case 0x2c:
            ret = dev->adc.samp_ct;
            break;
        case 0x2e:
            ret = dev->adc.curr_samp_ct;
            break;

        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            ret = es137x_read_frame_reg(dev, port & 0x3c, dev->mem_page) & 0xffff;
            break;
        case 0x32:
        case 0x36:
        case 0x3a:
        case 0x3e:
            ret = es137x_read_frame_reg(dev, port & 0x3c, dev->mem_page) >> 16;
            break;

        default:
            break;
    }

    audiopci_log("es137x_inw: port=%04x ret=%04x\n", port, ret);

    return ret;
}

static uint32_t
es137x_inl(uint16_t port, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    uint32_t  ret = 0xffffffff;

    if ((dev->type == AUDIOPCI_ES1370) && (port & 0x3c) == 0x14)
        port = 0x10;

    switch (port & 0x3c) {
        /* Interrupt/Chip Select Control Register, Address 00H
           Addressable as byte, word, longword */
        case 0x00:
            ret = dev->int_ctrl & 0xff0fffff;
            if ((ret < AUDIOPCI_ES1373) && (ret != AUDIOPCI_ES1370))
                ret |= 0xfc000000;
            break;

        /* Interrupt/Chip Select Status Register, Address 04H
           Addressable as longword only */
        case 0x04:
            ret = dev->int_status;
            audiopci_log("[R] STATUS = %08X\n", ret);
            break;

        /* Memory Page Register, Address 0CH
           Addressable as byte, word, longword */
        case 0x0c:
            ret = dev->mem_page;
            break;

        /* Sample Rate Converter Interface Register, Address 10H
           Addressable as longword only */
        case 0x10:
            ret = dev->sr_cir & ~0xffff;
            ret |= dev->sr_ram[dev->sr_cir >> 25];
            break;

        /* CODEC Read Register, Address 14H
           Addressable as longword only */
        case 0x14:
            if (dev->type == AUDIOPCI_ES1370)
                break;
            ret = dev->codec_ctrl | CODEC_READY;
            break;

        /* Legacy Control/Status Register, Address 18H
           Addressable as byte, word, longword */
        case 0x18:
            ret = (dev->legacy_ctrl & 0xffff07fd) | 0x0000f800;
            break;

        /* S/PDIF Channel Status Control Register, Address 1CH
           Addressable as byte, word, longword */
        case 0x1c:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                ret = dev->spdif_chstatus;
            break;

        /* Serial Interface Control Register, Address 20H
            Addressable as byte, word, longword */
        case 0x20:
            ret = dev->si_cr;
            if (dev->type != AUDIOPCI_ES1370)
                ret |= 0xff800000;
            break;

        /* DAC1 Channel Sample Count Register, Address 24H
           Addressable as word, longword */
        case 0x24:
            ret = dev->dac[0].samp_ct | (((uint32_t) dev->dac[0].curr_samp_ct) << 16);
            break;

        /* DAC2 Channel Sample Count Register, Address 28H
           Addressable as word, longword */
        case 0x28:
            ret = dev->dac[1].samp_ct | (((uint32_t) dev->dac[1].curr_samp_ct) << 16);
            break;

        /* ADC Channel Sample Count Register, Address 2CH
           Addressable as word, longword */
        case 0x2c:
            ret = dev->adc.samp_ct | (((uint32_t) dev->adc.curr_samp_ct) << 16);
            break;

        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            ret = es137x_read_frame_reg(dev, port & 0x3c, dev->mem_page);
            break;

        default:
            break;
    }

    audiopci_log("es137x_inl: port=%04x ret=%08x\n", port, ret);
    return ret;
}

static void
es137x_outb(uint16_t port, uint8_t val, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    uint32_t  old_legacy_ctrl;

    audiopci_log("es137x_outb: port=%04x val=%02x\n", port, val);

    switch (port & 0x3f) {
        /* Interrupt/Chip Select Control Register, Address 00H
           Addressable as byte, word, longword */
        case 0x00:
            if (!(dev->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN)) {
                dev->dac[0].addr           = dev->dac[0].addr_latch;
                dev->dac[0].buffer_pos     = 0;
                dev->dac[0].buffer_pos_end = 0;
                dev->dac[0].prev_out_l     = 0;
                dev->dac[0].prev_out_r     = 0;
                es137x_fetch(dev, 0);
            }
            if (!(dev->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN)) {
                dev->dac[1].addr           = dev->dac[1].addr_latch;
                dev->dac[1].buffer_pos     = 0;
                dev->dac[1].buffer_pos_end = 0;
                dev->dac[1].prev_out_l     = 0;
                dev->dac[1].prev_out_r     = 0;
                es137x_fetch(dev, 1);
            }
            // audiopci_log("INTCTRL 0x%02X\n", val & 0xff);
            dev->int_ctrl = (dev->int_ctrl & 0xffffff00) | val;
            break;
        case 0x01:
            dev->int_ctrl = (dev->int_ctrl & 0xffff00ff) | (val << 8);
            es1370_calc_sample_rate(dev);
            break;
        case 0x02:
            dev->int_ctrl = (dev->int_ctrl & 0xff00ffff) | (val << 16);
            es1370_calc_sample_rate(dev);
            break;
        case 0x03:
            dev->int_ctrl = (dev->int_ctrl & 0x00ffffff) | (val << 24);
            gameport_remap(dev->gameport, 0x200 | ((val & 0x03) << 3));
            es1370_calc_sample_rate(dev);
            break;

        /* Interrupt/Chip Select Status Register, Address 04H
           Addressable as longword only, but PCem implements byte access, which
           must be for a reason */
        case 0x06:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                dev->int_status = (dev->int_status & 0xff08ffff) | (val << 16);
            break;
        case 0x07:
            if (dev->type == AUDIOPCI_CT5880)
                dev->int_status = (dev->int_status & 0xd2ffffff) | (val << 24);
            break;

        /* UART Data Register, Address 08H
           Addressable as byte only */
        case 0x08:
            audiopci_log("MIDI data = %02x\n", val);
            /* TX does not use FIFO. */
            midi_raw_out_byte(val);
            es137x_set_tx_irq(dev, 1);
            break;

        /* UART Control Register, Address 09H
           Addressable as byte only */
        case 0x09:
            audiopci_log("[W] UART CTRL = %02X\n", val);
            dev->uart_ctrl = val & 0xe3;

            if ((val & 0x03) == 0x03) {
                /* Reset TX */
                es137x_set_tx_irq(dev, 1);

                /* Software reset */
                es137x_reset_fifo(dev);
            } else {
                es137x_set_tx_irq(dev, 1);

                es137x_update_tx_irq(dev);
                es137x_update_rx_irq(dev);
            }
            break;

        /* UART Reserved Register, Address 0AH
           Addressable as byte only */
        case 0x0a:
            audiopci_log("[W] UART RES = %02X\n", val);
            dev->uart_res = val & 0x01;
            break;

        /* Memory Page Register, Address 0CH
           Addressable as byte, word, longword */
        case 0x0c:
            dev->mem_page = val & 0xf;
            break;
        case 0x0d ... 0x0f:
            break;

        /* Legacy Control/Status Register, Address 18H
           Addressable as byte, word, longword */
        case 0x18:
            dev->legacy_ctrl |= LEGACY_INT;
            break;
        case 0x1a:
            old_legacy_ctrl  = dev->legacy_ctrl;
            dev->legacy_ctrl = (dev->legacy_ctrl & 0xff00ffff) | (val << 16);
            update_legacy(dev, old_legacy_ctrl);
            break;
        case 0x1b:
            old_legacy_ctrl  = dev->legacy_ctrl;
            dev->legacy_ctrl = (dev->legacy_ctrl & 0x00ffffff) | (val << 24);
            es137x_update_irqs(dev);
            update_legacy(dev, old_legacy_ctrl);
            break;

        /* S/PDIF Channel Status Control Register, Address 1CH
           Addressable as byte, word, longword */
        case 0x1c:
            dev->spdif_chstatus = (dev->spdif_chstatus & 0xffffff00) | val;
            break;
        case 0x1d:
            dev->spdif_chstatus = (dev->spdif_chstatus & 0xffff00ff) | (val << 8);
            break;
        case 0x1e:
            dev->spdif_chstatus = (dev->spdif_chstatus & 0xff00ffff) | (val << 16);
            break;
        case 0x1f:
            dev->spdif_chstatus = (dev->spdif_chstatus & 0x00ffffff) | (val << 24);
            break;

        /* Serial Interface Control Register, Address 20H
            Addressable as byte, word, longword */
        case 0x20:
            if (dev->type == AUDIOPCI_ES1370)
                dev->si_cr = (dev->si_cr & 0xffff00) | val;
            else
                dev->si_cr = (dev->si_cr & 0xffffff00) | val;
            break;
        case 0x21:
            if (dev->type == AUDIOPCI_ES1370)
                dev->si_cr = (dev->si_cr & 0xff00ff) | (val << 8);
            else
                dev->si_cr = (dev->si_cr & 0xffff00ff) | (val << 8);
            if (!(dev->si_cr & SI_P1_INTR_EN))
                dev->int_status &= ~INT_STATUS_DAC1;
            if (!(dev->si_cr & SI_P2_INTR_EN))
                dev->int_status &= ~INT_STATUS_DAC2;

            es137x_update_irqs(dev);
            break;
        case 0x22:
            if (dev->type == AUDIOPCI_ES1370)
                dev->si_cr = (dev->si_cr & 0xc0ffff) | ((val & 0x3f) << 16);
            else
                dev->si_cr = (dev->si_cr & 0xff80ffff) | ((val & 0x7f) << 16);
            break;

        default:
            audiopci_log("Bad es137x_outb: port=%04x val=%02x\n", port, val);
    }
}

static void
es137x_outw(uint16_t port, uint16_t val, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    uint32_t  old_legacy_ctrl;

    switch (port & 0x3f) {
        /* Interrupt/Chip Select Control Register, Address 00H
           Addressable as byte, word, longword */
        case 0x00:
            if (!(dev->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN)) {
                dev->dac[0].addr           = dev->dac[0].addr_latch;
                dev->dac[0].buffer_pos     = 0;
                dev->dac[0].buffer_pos_end = 0;
                dev->dac[0].prev_out_l     = 0;
                dev->dac[0].prev_out_r     = 0;
                dev->step_synth            = dev->interp_step_synth;
                es137x_fetch(dev, 0);
            }
            if (!(dev->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN)) {
                dev->dac[1].addr           = dev->dac[1].addr_latch;
                dev->dac[1].buffer_pos     = 0;
                dev->dac[1].buffer_pos_end = 0;
                dev->dac[1].prev_out_l     = 0;
                dev->dac[1].prev_out_r     = 0;
                dev->step_pcm              = dev->interp_step;
                es137x_fetch(dev, 1);
            }
            // audiopci_log("INTCTRL 0x%02X\n", val & 0xff);
            dev->int_ctrl = (dev->int_ctrl & 0xffff0000) | val;
            break;
        case 0x02:
            dev->int_ctrl = (dev->int_ctrl & 0x0000ffff) | (val << 16);
            gameport_remap(dev->gameport, 0x200 | ((val & 0x0300) >> 5));
            es1370_calc_sample_rate(dev);
            break;

        /* Memory Page Register, Address 0CH
           Addressable as byte, word, longword */
        case 0x0c:
            dev->mem_page = val & 0xf;
            break;
        case 0x0e:
            break;

        /* CODEC Write Register, Address 10H
           Addressable as word, longword */
        case 0x10:
            if (dev->type != AUDIOPCI_ES1370)
                break;

            dev->akm_codec.registers[(val >> 8) & 0xFF] = val & 0xFF;
            if ((val >> 8) == 0x16 && !(val & 1))
                akm4531_reset(dev);
            break;

        /* Legacy Control/Status Register, Address 18H
           Addressable as byte, word, longword */
        case 0x18:
            dev->legacy_ctrl |= LEGACY_INT;
            break;
        case 0x1a:
            old_legacy_ctrl  = dev->legacy_ctrl;
            dev->legacy_ctrl = (dev->legacy_ctrl & 0x0000ffff) | (val << 16);
            es137x_update_irqs(dev);
            update_legacy(dev, old_legacy_ctrl);
            break;

        /* S/PDIF Channel Status Control Register, Address 1CH
           Addressable as byte, word, longword */
        case 0x1c:
            dev->spdif_chstatus = (dev->spdif_chstatus & 0xffff0000) | val;
            break;
        case 0x1e:
            dev->spdif_chstatus = (dev->spdif_chstatus & 0x0000ffff) | (val << 16);
            break;

        /* Serial Interface Control Register, Address 20H
            Addressable as byte, word, longword */
        case 0x20:
            if (dev->type == AUDIOPCI_ES1370)
                dev->si_cr = (dev->si_cr & 0xff0000) | val;
            else
                dev->si_cr = (dev->si_cr & 0xffff0000) | val;

            if (!(dev->si_cr & SI_P1_INTR_EN))
                dev->int_status &= ~INT_STATUS_DAC1;
            if (!(dev->si_cr & SI_P2_INTR_EN))
                dev->int_status &= ~INT_STATUS_DAC2;
            es137x_update_irqs(dev);
            break;
        case 0x22:
            if (dev->type == AUDIOPCI_ES1370)
                dev->si_cr = (dev->si_cr & 0xc0ffff) | ((val & 0x3f) << 16);
            else
                dev->si_cr = (dev->si_cr & 0xff80ffff) | ((val & 0x007f) << 16);
            break;

        /* DAC1 Channel Sample Count Register, Address 24H
           Addressable as word, longword */
        case 0x24:
            dev->dac[0].samp_ct = val;
            break;

        /* DAC2 Channel Sample Count Register, Address 28H
           Addressable as word, longword */
        case 0x28:
            dev->dac[1].samp_ct = val;
            break;

        /* ADC Channel Sample Count Register, Address 2CH
           Addressable as word, longword */
        case 0x2c:
            dev->adc.samp_ct = val;
            break;

        default:
            break;
    }
}

static void
es137x_outl(uint16_t port, uint32_t val, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    uint32_t  old_legacy_ctrl;

    audiopci_log("es137x_outl: port=%04x val=%08x\n", port, val);

    switch (port & 0x3f) {
        /* Interrupt/Chip Select Control Register, Address 00H
           Addressable as byte, word, longword */
        case 0x00:
            {
                uint8_t dac1start = 0;
                uint8_t dac2start = 0;

                if (!(dev->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN)) {
                    dev->dac[0].addr           = dev->dac[0].addr_latch;
                    dev->dac[0].buffer_pos     = 0;
                    dev->dac[0].buffer_pos_end = 0;
                    dev->dac[0].prev_out_l     = 0;
                    dev->dac[0].prev_out_r     = 0;
                    dac1start                  = 1;
                    es137x_fetch(dev, 0);
                }
                if (!(dev->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN)) {
                    dev->dac[1].addr           = dev->dac[1].addr_latch;
                    dev->dac[1].buffer_pos     = 0;
                    dev->dac[1].buffer_pos_end = 0;
                    dev->dac[1].prev_out_l     = 0;
                    dev->dac[1].prev_out_r     = 0;
                    dac2start                  = 1;
                    es137x_fetch(dev, 1);
                }
                // audiopci_log("INTCTRL 0x%02X\n", val & 0xff);
                dev->int_ctrl = val;
                gameport_remap(dev->gameport, 0x200 | ((val & 0x03000000) >> 21));
                es1370_calc_sample_rate(dev);
                if (dac1start)
                    dev->step_synth = dev->interp_step_synth;
                if (dac2start)
                    dev->step_pcm = dev->interp_step;
                break;
            }

        /* Interrupt/Chip Select Status Register, Address 04H
           Addressable as longword only */
        case 0x04:
            audiopci_log("[W] STATUS = %08X\n", val);
            if (dev->type == AUDIOPCI_CT5880)
                dev->int_status = (dev->int_status & 0xd208ffff) | (val & 0x2df70000);
            else if (dev->type == AUDIOPCI_ES1373)
                dev->int_status = (dev->int_status & 0xff08ffff) | (val & 0x00f70000);
            break;

        /* Memory Page Register, Address 0CH
           Addressable as byte, word, longword */
        case 0x0c:
            dev->mem_page = val & 0xf;
            break;

        /* Sample Rate Converter Interface Register, Address 10H
           Addressable as longword only */
        case 0x10:
            if (dev->type == AUDIOPCI_ES1370) {
                dev->akm_codec.registers[(val >> 8) & 0xFF] = val & 0xFF;
                if ((val >> 8) == 0x16 && !(val & 1))
                    akm4531_reset(dev);
                break;
            }
            dev->sr_cir = val & 0xfff8ffff; /*Bits 16 to 18 are undefined*/
            if (dev->sr_cir & SRC_RAM_WE) {
                dev->sr_ram[dev->sr_cir >> 25] = val & 0xffff;
                switch (dev->sr_cir >> 25) {
                    case 0x71:
                        dev->dac[0].vf    = (dev->dac[0].vf & ~0x1f8000) | ((val & 0xfc00) << 5);
                        dev->dac[0].ac    = (dev->dac[0].ac & ~0x7f8000) | ((val & 0x00ff) << 15);
                        dev->dac[0].f_pos = 0;
                        break;
                    case 0x72:
                        dev->dac[0].ac = (dev->dac[0].ac & ~0x7fff) | (val & 0x7fff);
                        break;
                    case 0x73:
                        dev->dac[0].vf = (dev->dac[0].vf & ~0x7fff) | (val & 0x7fff);
                        break;

                    case 0x75:
                        dev->dac[1].vf    = (dev->dac[1].vf & ~0x1f8000) | ((val & 0xfc00) << 5);
                        dev->dac[1].ac    = (dev->dac[1].ac & ~0x7f8000) | ((val & 0x00ff) << 15);
                        dev->dac[1].f_pos = 0;
                        break;
                    case 0x76:
                        dev->dac[1].ac = (dev->dac[1].ac & ~0x7fff) | (val & 0x7fff);
                        break;
                    case 0x77:
                        dev->dac[1].vf = (dev->dac[1].vf & ~0x7fff) | (val & 0x7fff);
                        break;

                    case 0x7c:
                        dev->dac[0].vol_l = (int32_t) (int16_t) (val & 0xffff);
                        break;
                    case 0x7d:
                        dev->dac[0].vol_r = (int32_t) (int16_t) (val & 0xffff);
                        break;
                    case 0x7e:
                        dev->dac[1].vol_l = (int32_t) (int16_t) (val & 0xffff);
                        break;
                    case 0x7f:
                        dev->dac[1].vol_r = (int32_t) (int16_t) (val & 0xffff);
                        break;

                    default:
                        break;
                }
            }
            break;

        /* CODEC Write Register, Address 14H
           Addressable as longword only */
        case 0x14:
            if (dev->type == AUDIOPCI_ES1370)
                break;
            if (val & CODEC_READ) {
                dev->codec_ctrl &= 0x00ff0000;
                dev->codec_ctrl |= ac97_codec_readw(dev->codec, val >> 16);
            } else {
                dev->codec_ctrl = val & 0x00ffffff;
                ac97_codec_writew(dev->codec, val >> 16, val);

                ac97_codec_getattn(dev->codec, 0x02, &dev->master_vol_l, &dev->master_vol_r);
                ac97_codec_getattn(dev->codec, 0x18, &dev->pcm_vol_l, &dev->pcm_vol_r);
                ac97_codec_getattn(dev->codec, 0x38, &dev->pcm_rear_vol_l, &dev->pcm_rear_vol_r);
                ac97_codec_getattn(dev->codec, 0x12, &dev->cd_vol_l, &dev->cd_vol_r);
            }
            break;

        /* Legacy Control/Status Register, Address 18H
           Addressable as byte, word, longword */
        case 0x18:
            old_legacy_ctrl  = dev->legacy_ctrl;
            dev->legacy_ctrl = (dev->legacy_ctrl & 0x0000ffff) | (val & 0xffff0000);
            dev->legacy_ctrl |= LEGACY_INT;
            es137x_update_irqs(dev);
            update_legacy(dev, old_legacy_ctrl);
            break;

        /* S/PDIF Channel Status Control Register, Address 1CH
           Addressable as byte, word, longword */
        case 0x1c:
            dev->spdif_chstatus = val;
            break;

        /* Serial Interface Control Register, Address 20H
            Addressable as byte, word, longword */
        case 0x20:
            if (dev->type == AUDIOPCI_ES1370)
                dev->si_cr = val & 0x3fffff;
            else
                dev->si_cr = (val & 0x007fffff) | 0xff800000;
            if (!(dev->si_cr & SI_P1_INTR_EN))
                dev->int_status &= ~INT_STATUS_DAC1;
            if (!(dev->si_cr & SI_P2_INTR_EN))
                dev->int_status &= ~INT_STATUS_DAC2;
            es137x_update_irqs(dev);
            break;

        /* DAC1 Channel Sample Count Register, Address 24H
           Addressable as word, longword */
        case 0x24:
            dev->dac[0].samp_ct = val & 0xffff;
            break;

        /* DAC2 Channel Sample Count Register, Address 28H
           Addressable as word, longword */
        case 0x28:
            dev->dac[1].samp_ct = val & 0xffff;
            break;

        /* ADC Channel Sample Count Register, Address 2CH
           Addressable as word, longword */
        case 0x2c:
            dev->adc.samp_ct = val & 0xffff;
            break;

        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            es137x_write_frame_reg(dev, port & 0x3c, dev->mem_page, val);
            break;

        default:
            break;
    }
}

static void
capture_event(es137x_t *dev, int type, int rw, uint16_t port)
{
    dev->legacy_ctrl &= ~(LEGACY_EVENT_MASK | LEGACY_EVENT_ADDR_MASK);
    dev->legacy_ctrl |= type;
    if (rw)
        dev->legacy_ctrl |= LEGACY_EVENT_TYPE_RW;
    else
        dev->legacy_ctrl &= ~LEGACY_EVENT_TYPE_RW;
    dev->legacy_ctrl |= ((port << LEGACY_EVENT_ADDR_SHIFT) & LEGACY_EVENT_ADDR_MASK);
    dev->legacy_ctrl &= ~LEGACY_INT;
    nmi_raise();
}

static void
capture_write_sscape(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_SSCAPE, 1, port);
}

static void
capture_write_codec(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_CODEC, 1, port);
}

static void
capture_write_sb(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_SB, 1, port);
}

static void
capture_write_adlib(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_ADLIB, 1, port);
}

static void
capture_write_master_pic(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_MASTER_PIC, 1, port);
}

static void
capture_write_master_dma(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_MASTER_DMA, 1, port);
}

static void
capture_write_slave_pic(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_SLAVE_PIC, 1, port);
}

static void
capture_write_slave_dma(uint16_t port, UNUSED(uint8_t val), void *priv)
{
    capture_event(priv, LEGACY_EVENT_SLAVE_DMA, 1, port);
}

static uint8_t
capture_read_sscape(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_SSCAPE, 0, port);
    return 0xff;
}

static uint8_t
capture_read_codec(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_CODEC, 0, port);
    return 0xff;
}

static uint8_t
capture_read_sb(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_SB, 0, port);
    return 0xff;
}

static uint8_t
capture_read_adlib(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_ADLIB, 0, port);
    return 0xff;
}

static uint8_t
capture_read_master_pic(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_MASTER_PIC, 0, port);
    return 0xff;
}

static uint8_t
capture_read_master_dma(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_MASTER_DMA, 0, port);
    return 0xff;
}

static uint8_t
capture_read_slave_pic(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_SLAVE_PIC, 0, port);
    return 0xff;
}

static uint8_t
capture_read_slave_dma(uint16_t port, void *priv)
{
    capture_event(priv, LEGACY_EVENT_SLAVE_DMA, 0, port);
    return 0xff;
}

static void
update_legacy(es137x_t *dev, uint32_t old_legacy_ctrl)
{
    if (old_legacy_ctrl & LEGACY_CAPTURE_SSCAPE) {
        switch ((old_legacy_ctrl >> LEGACY_SSCAPE_ADDR_SHIFT) & 3) {
            case 0:
                io_removehandler(0x0320, 0x0008,
                                 capture_read_sscape, NULL, NULL,
                                 capture_write_sscape, NULL, NULL, dev);
                break;
            case 1:
                io_removehandler(0x0330, 0x0008,
                                 capture_read_sscape, NULL, NULL,
                                 capture_write_sscape, NULL, NULL, dev);
                break;
            case 2:
                io_removehandler(0x0340, 0x0008,
                                 capture_read_sscape, NULL, NULL,
                                 capture_write_sscape, NULL, NULL, dev);
                break;
            case 3:
                io_removehandler(0x0350, 0x0008,
                                 capture_read_sscape, NULL, NULL,
                                 capture_write_sscape, NULL, NULL, dev);
                break;

            default:
                break;
        }
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_CODEC) {
        switch ((old_legacy_ctrl >> LEGACY_CODEC_ADDR_SHIFT) & 3) {
            case 0:
                io_removehandler(0x0530, 0x0008,
                                 capture_read_codec, NULL, NULL,
                                 capture_write_codec, NULL, NULL, dev);
                break;
            case 2:
                io_removehandler(0x0e80, 0x0008,
                                 capture_read_codec, NULL, NULL,
                                 capture_write_codec, NULL, NULL, dev);
                break;
            case 3:
                io_removehandler(0x0f40, 0x0008,
                                 capture_read_codec, NULL, NULL,
                                 capture_write_codec, NULL, NULL, dev);
                break;

            default:
                break;
        }
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_SB) {
        if (!(old_legacy_ctrl & LEGACY_SB_ADDR)) {
            io_removehandler(0x0220, 0x0010,
                             capture_read_sb, NULL, NULL,
                             capture_write_sb, NULL, NULL, dev);
        } else {
            io_removehandler(0x0240, 0x0010,
                             capture_read_sb, NULL, NULL,
                             capture_write_sb, NULL, NULL, dev);
        }
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_ADLIB) {
        io_removehandler(0x0388, 0x0004,
                         capture_read_adlib, NULL, NULL,
                         capture_write_adlib, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_MASTER_PIC) {
        io_removehandler(0x0020, 0x0002,
                         capture_read_master_pic, NULL, NULL,
                         capture_write_master_pic, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_MASTER_DMA) {
        io_removehandler(0x0000, 0x0010,
                         capture_read_master_dma, NULL, NULL,
                         capture_write_master_dma, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_SLAVE_PIC) {
        io_removehandler(0x00a0, 0x0002,
                         capture_read_slave_pic, NULL, NULL,
                         capture_write_slave_pic, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_SLAVE_DMA) {
        io_removehandler(0x00c0, 0x0020,
                         capture_read_slave_dma, NULL, NULL,
                         capture_write_slave_dma, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SSCAPE) {
        switch ((dev->legacy_ctrl >> LEGACY_SSCAPE_ADDR_SHIFT) & 3) {
            case 0:
                io_sethandler(0x0320, 0x0008,
                              capture_read_sscape, NULL, NULL,
                              capture_write_sscape, NULL, NULL, dev);
                break;
            case 1:
                io_sethandler(0x0330, 0x0008,
                              capture_read_sscape, NULL, NULL,
                              capture_write_sscape, NULL, NULL, dev);
                break;
            case 2:
                io_sethandler(0x0340, 0x0008,
                              capture_read_sscape, NULL, NULL,
                              capture_write_sscape, NULL, NULL, dev);
                break;
            case 3:
                io_sethandler(0x0350, 0x0008,
                              capture_read_sscape, NULL, NULL,
                              capture_write_sscape, NULL, NULL, dev);
                break;

            default:
                break;
        }
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_CODEC) {
        switch ((dev->legacy_ctrl >> LEGACY_CODEC_ADDR_SHIFT) & 3) {
            case 0:
                io_sethandler(0x0530, 0x0008,
                              capture_read_codec, NULL, NULL,
                              capture_write_codec, NULL, NULL, dev);
                break;
            case 2:
                io_sethandler(0x0e80, 0x0008,
                              capture_read_codec, NULL, NULL,
                              capture_write_codec, NULL, NULL, dev);
                break;
            case 3:
                io_sethandler(0x0f40, 0x0008,
                              capture_read_codec, NULL, NULL,
                              capture_write_codec, NULL, NULL, dev);
                break;

            default:
                break;
        }
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SB) {
        if (!(dev->legacy_ctrl & LEGACY_SB_ADDR)) {
            io_sethandler(0x0220, 0x0010,
                          capture_read_sb, NULL, NULL,
                          capture_write_sb, NULL, NULL, dev);
        } else {
            io_sethandler(0x0240, 0x0010,
                          capture_read_sb, NULL, NULL,
                          capture_write_sb, NULL, NULL, dev);
        }
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_ADLIB) {
        io_sethandler(0x0388, 0x0004,
                      capture_read_adlib, NULL, NULL,
                      capture_write_adlib, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_MASTER_PIC) {
        io_sethandler(0x0020, 0x0002,
                      capture_read_master_pic, NULL, NULL,
                      capture_write_master_pic, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_MASTER_DMA) {
        io_sethandler(0x0000, 0x0010,
                      capture_read_master_dma, NULL, NULL,
                      capture_write_master_dma, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SLAVE_PIC) {
        io_sethandler(0x00a0, 0x0002,
                      capture_read_slave_pic, NULL, NULL,
                      capture_write_slave_pic, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SLAVE_DMA) {
        io_sethandler(0x00c0, 0x0020,
                      capture_read_slave_dma, NULL, NULL,
                      capture_write_slave_dma, NULL, NULL, dev);
    }
}

static uint8_t
es1370_pci_read(int func, int addr, void *priv)
{
    const es137x_t *dev = (es137x_t *) priv;

    if (func > 0)
        return 0xff;

    if ((addr > 0x3f) && ((addr < 0xdc) || (addr > 0xe1)))
        return 0x00;

    switch (addr) {
        case 0x00:       /* Vendor ID */
            return 0x74; /* Ensoniq */
        case 0x01:
            return 0x12;

        case 0x02:       /* Device ID */
            return dev->type >> 16; /* ES1370 */
        case 0x03:
            return dev->type >> 24;

        case 0x04: /* Command TODO */
            return dev->pci_command;
        case 0x05:
            return dev->pci_serr;

        case 0x06:       /* Status TODO */
            return 0x10; /* Supports ACPI */
        case 0x07:
            return 0x00;

        case 0x08:       /* Class Code & Revision ID */
            return dev->type >> 8; /* Revision ID - 0x00 is actual Ensoniq-branded ES1370 */
        case 0x09:
            return 0x00; /* Multimedia audio device */
        case 0x0a:
            return 0x01;
        case 0x0b:
            return 0x04;

//        case 0x0c: /* Cache Line Size TODO */
//        case 0x0d: /* Latency Timer TODO */
//        case 0x0e: /* Header Type TODO */
//        case 0x0f: /* BIST TODO */

        case 0x10:                                 /* Base Address TODO */
            return 0x01 | (dev->base_addr & 0xc0); /* memBaseAddr */
        case 0x11:
            return dev->base_addr >> 8;
        case 0x12:
            return dev->base_addr >> 16;
        case 0x13:
            return dev->base_addr >> 24;

        case 0x2c ... 0x2f:
            return dev->subsys_id[addr & 3]; /* Subsystem vendor ID */

#if 0
        case 0x34: // TODO
            return 0xdc; /* Capabilites pointer */
#endif

        case 0x3c:
            return dev->int_line;
        case 0x3d:
            return 0x01; /* INTA */

        case 0x3e:
            return 0xc; /* Minimum grant */
        case 0x3f:
            return 0x80; /* Maximum latency */

#if 0
        case 0xdc:
            return 0x01; /* Capabilities identifier */
        case 0xdd:
            return 0x00; /* Next item pointer */
        case 0xde:
            return 0x31; /* Power management capabilities */
        case 0xdf:
            return 0x6c;

        case 0xe0:
            return dev->pmcsr & 0xff;
        case 0xe1:
            return dev->pmcsr >> 8;
#endif

        default:
            break;
    }

    return 0x00;
}

static uint8_t
es1371_pci_read(int func, int addr, void *priv)
{
    const es137x_t *dev = (es137x_t *) priv;

    if (func > 0)
        return 0xff;

    if ((addr > 0x3f) && ((addr < 0xdc) || (addr > 0xe1)))
        return 0x00;

    switch (addr) {
        case 0x00:
            return 0x74; /* Ensoniq */
        case 0x01:
            return 0x12;

        case 0x02:
            return dev->type >> 16; /* ES1371 */
        case 0x03:
            return dev->type >> 24;

        case 0x04:
            return dev->pci_command;
        case 0x05:
            return dev->pci_serr;

        case 0x06:
            return 0x10; /* Supports ACPI */
        case 0x07:
            return 0x00;

        case 0x08:
            return dev->type >> 8; /* Revision ID */
        case 0x09:
            return 0x00; /* Multimedia audio device */
        case 0x0a:
            return 0x01;
        case 0x0b:
            return 0x04;

        case 0x10:
            return 0x01 | (dev->base_addr & 0xc0); /* memBaseAddr */
        case 0x11:
            return dev->base_addr >> 8;
        case 0x12:
            return dev->base_addr >> 16;
        case 0x13:
            return dev->base_addr >> 24;

        case 0x2c ... 0x2f:
            return dev->subsys_id[addr & 3]; /* Subsystem vendor ID */

        case 0x34:
            return 0xdc; /* Capabilites pointer */

        case 0x3c:
            return dev->int_line;
        case 0x3d:
            return 0x01; /* INTA */

        case 0x3e:
            return 0xc; /* Minimum grant */
        case 0x3f:
            return 0x80; /* Maximum latency */

        case 0x40:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                return dev->subsys_lock;
            break;

        case 0xdc:
            return 0x01; /* Capabilities identifier */
        case 0xdd:
            return 0x00; /* Next item pointer */
        case 0xde:
            return 0x31; /* Power management capabilities */
        case 0xdf:
            return 0x6c;

        case 0xe0:
            return dev->pmcsr & 0xff;
        case 0xe1:
            return dev->pmcsr >> 8;

        default:
            break;
    }

    return 0x00;
}

static void
es137x_io_set(es137x_t *dev, int set)
{
    if (dev->pci_command & PCI_COMMAND_IO) {
        io_handler(set, dev->base_addr, 0x0040,
                   es137x_inb, es137x_inw, es137x_inl,
                   es137x_outb, es137x_outw, es137x_outl, dev);
    }
}

static void
es1370_pci_write(int func, int addr, uint8_t val, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;

    if (func)
        return;

    switch (addr) {
        case 0x04:
            es137x_io_set(dev, 0);
            dev->pci_command = val & 0x05;
            es137x_io_set(dev, 1);
            break;
        case 0x05:
            dev->pci_serr = val & 1;
            break;

        case 0x10:
            es137x_io_set(dev, 0);
            dev->base_addr = (dev->base_addr & 0xffffff00) | (val & 0xc0);
            es137x_io_set(dev, 1);
            break;
        case 0x11:
            es137x_io_set(dev, 0);
            dev->base_addr = (dev->base_addr & 0xffff00c0) | (val << 8);
            es137x_io_set(dev, 1);
            break;
        case 0x12:
            dev->base_addr = (dev->base_addr & 0xff00ffc0) | (val << 16);
            break;
        case 0x13:
            dev->base_addr = (dev->base_addr & 0x00ffffc0) | (val << 24);
            break;

        case 0x3c:
            dev->int_line = val;
            break;

        case 0xe0:
            dev->pmcsr = (dev->pmcsr & 0xff00) | (val & 0x03);
            break;
        case 0xe1:
            dev->pmcsr = (dev->pmcsr & 0x00ff) | ((val & 0x01) << 8);
            break;

        default:
            break;
    }
}

static void
es1371_pci_write(int func, int addr, uint8_t val, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;

    if (func)
        return;

    switch (addr) {
        case 0x04:
            es137x_io_set(dev, 0);
            dev->pci_command = val & 0x05;
            es137x_io_set(dev, 1);
            break;
        case 0x05:
            dev->pci_serr = val & 1;
            break;

        case 0x10:
            es137x_io_set(dev, 0);
            dev->base_addr = (dev->base_addr & 0xffffff00) | (val & 0xc0);
            es137x_io_set(dev, 1);
            break;
        case 0x11:
            es137x_io_set(dev, 0);
            dev->base_addr = (dev->base_addr & 0xffff00c0) | (val << 8);
            es137x_io_set(dev, 1);
            break;
        case 0x12:
            dev->base_addr = (dev->base_addr & 0xff00ffc0) | (val << 16);
            break;
        case 0x13:
            dev->base_addr = (dev->base_addr & 0x00ffffc0) | (val << 24);
            break;

        case 0x2c ... 0x2f:
            if (dev->subsys_lock == 0xea)
                dev->subsys_id[addr & 3] = val;
            break;

        case 0x3c:
            dev->int_line = val;
            break;

        case 0x40:
            if ((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880))
                dev->subsys_lock = val;
            break;

        case 0xe0:
            dev->pmcsr = (dev->pmcsr & 0xff00) | (val & 0x03);
            break;
        case 0xe1:
            dev->pmcsr = (dev->pmcsr & 0x00ff) | ((val & 0x01) << 8);
            break;

        default:
            break;
    }
}

static void
es137x_fetch(es137x_t *dev, int dac_nr)
{
    if (dev->si_cr & (dac_nr ? SI_P2_PAUSE : SI_P1_PAUSE))
        return;

    int format = dac_nr ? ((dev->si_cr >> 2) & 3) : (dev->si_cr & 3);
    int pos    = dev->dac[dac_nr].buffer_pos & 63;
    int c;

    switch (format) {
        case FORMAT_MONO_8:
            for (c = 0; c < 32; c += 4) {
                dev->dac[dac_nr].buffer_l[(pos + c) & 63] = dev->dac[dac_nr].buffer_r[(pos + c) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr) ^ 0x80) << 8;
                dev->dac[dac_nr].buffer_l[(pos + c + 1) & 63] = dev->dac[dac_nr].buffer_r[(pos + c + 1) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 1) ^ 0x80) << 8;
                dev->dac[dac_nr].buffer_l[(pos + c + 2) & 63] = dev->dac[dac_nr].buffer_r[(pos + c + 2) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 2) ^ 0x80) << 8;
                dev->dac[dac_nr].buffer_l[(pos + c + 3) & 63] = dev->dac[dac_nr].buffer_r[(pos + c + 3) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 3) ^ 0x80) << 8;
                dev->dac[dac_nr].addr += 4;

                dev->dac[dac_nr].buffer_pos_end += 4;
                dev->dac[dac_nr].count++;

                if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
                    dev->dac[dac_nr].count = 0;
                    dev->dac[dac_nr].addr  = dev->dac[dac_nr].addr_latch;
                    break;
                }
            }
            break;

        case FORMAT_STEREO_8:
            for (c = 0; c < 16; c += 2) {
                dev->dac[dac_nr].buffer_l[(pos + c) & 63]     = (mem_readb_phys(dev->dac[dac_nr].addr) ^ 0x80) << 8;
                dev->dac[dac_nr].buffer_r[(pos + c) & 63]     = (mem_readb_phys(dev->dac[dac_nr].addr + 1) ^ 0x80) << 8;
                dev->dac[dac_nr].buffer_l[(pos + c + 1) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 2) ^ 0x80) << 8;
                dev->dac[dac_nr].buffer_r[(pos + c + 1) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 3) ^ 0x80) << 8;
                dev->dac[dac_nr].addr += 4;

                dev->dac[dac_nr].buffer_pos_end += 2;
                dev->dac[dac_nr].count++;

                if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
                    dev->dac[dac_nr].count = 0;
                    dev->dac[dac_nr].addr  = dev->dac[dac_nr].addr_latch;
                    break;
                }
            }
            break;

        case FORMAT_MONO_16:
            for (c = 0; c < 16; c += 2) {
                dev->dac[dac_nr].buffer_l[(pos + c) & 63] = dev->dac[dac_nr].buffer_r[(pos + c) & 63] = mem_readw_phys(dev->dac[dac_nr].addr);
                dev->dac[dac_nr].buffer_l[(pos + c + 1) & 63] = dev->dac[dac_nr].buffer_r[(pos + c + 1) & 63] = mem_readw_phys(dev->dac[dac_nr].addr + 2);
                dev->dac[dac_nr].addr += 4;

                dev->dac[dac_nr].buffer_pos_end += 2;
                dev->dac[dac_nr].count++;

                if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
                    dev->dac[dac_nr].count = 0;
                    dev->dac[dac_nr].addr  = dev->dac[dac_nr].addr_latch;
                    break;
                }
            }
            break;

        case FORMAT_STEREO_16:
            for (c = 0; c < 4; c++) {
                dev->dac[dac_nr].buffer_l[(pos + c) & 63] = mem_readw_phys(dev->dac[dac_nr].addr);
                dev->dac[dac_nr].buffer_r[(pos + c) & 63] = mem_readw_phys(dev->dac[dac_nr].addr + 2);
                dev->dac[dac_nr].addr += 4;

                dev->dac[dac_nr].buffer_pos_end++;
                dev->dac[dac_nr].count++;

                if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
                    dev->dac[dac_nr].count = 0;
                    dev->dac[dac_nr].addr  = dev->dac[dac_nr].addr_latch;
                    break;
                }
            }
            break;

        default:
            break;
    }
}

static inline float
low_fir_es1371(int dac_nr, int i, float NewSample)
{
    static float x[2][2][128]; // input samples
    static int   x_pos[2] = { 0, 0 };
    float        out      = 0.0;
    int          read_pos;
    int          n_coef;
    int          pos = x_pos[dac_nr];

    x[dac_nr][i][pos] = NewSample;

    /* Since only 1/16th of input samples are non-zero, only filter those that
       are valid.*/
    read_pos = (pos + 15) & (127 & ~15);
    n_coef   = (16 - pos) & 15;

    while (n_coef < ES137x_NCoef) {
        out += low_fir_es137x_coef[n_coef] * x[dac_nr][i][read_pos];
        read_pos = (read_pos + 16) & (127 & ~15);
        n_coef += 16;
    }

    if (i == 1) {
        x_pos[dac_nr] = (x_pos[dac_nr] + 1) & 127;
        if (x_pos[dac_nr] > 127)
            x_pos[dac_nr] = 0;
    }

    return out;
}

static void
es137x_next_sample_filtered(es137x_t *dev, int dac_nr, int out_idx)
{
    int out_l;
    int out_r;

    if ((dev->dac[dac_nr].buffer_pos - dev->dac[dac_nr].buffer_pos_end) >= 0)
        es137x_fetch(dev, dac_nr);

    out_l = dev->dac[dac_nr].buffer_l[dev->dac[dac_nr].buffer_pos & 63];
    out_r = dev->dac[dac_nr].buffer_r[dev->dac[dac_nr].buffer_pos & 63];

    dev->dac[dac_nr].filtered_l[out_idx] = (int) low_fir_es1371(dac_nr, 0, (float) out_l);
    dev->dac[dac_nr].filtered_r[out_idx] = (int) low_fir_es1371(dac_nr, 1, (float) out_r);

    for (uint8_t c = 1; c < 16; c++) {
        dev->dac[dac_nr].filtered_l[out_idx + c] = (int) low_fir_es1371(dac_nr, 0, 0);
        dev->dac[dac_nr].filtered_r[out_idx + c] = (int) low_fir_es1371(dac_nr, 1, 0);
    }

    dev->dac[dac_nr].buffer_pos++;
}

static void
es137x_update(es137x_t *dev)
{
    int32_t l;
    int32_t r;

    if (dev->type == AUDIOPCI_ES1370) {
        l = dev->dac[0].out_l * (((dev->akm_codec.registers[0x4] & 0x80) ? 0 : akm4531_gain_2dbstep_5bits[(dev->akm_codec.registers[0x4] & 0x1f)]) / 32767.0);
        r = dev->dac[0].out_r * (((dev->akm_codec.registers[0x5] & 0x80) ? 0 : akm4531_gain_2dbstep_5bits[(dev->akm_codec.registers[0x5] & 0x1f)]) / 32767.0);

        l += dev->dac[1].out_l * (((dev->akm_codec.registers[0x2] & 0x80) ? 0 : akm4531_gain_2dbstep_5bits[(dev->akm_codec.registers[0x2] & 0x1f)]) / 32767.0);
        r += dev->dac[1].out_r * (((dev->akm_codec.registers[0x3] & 0x80) ? 0 : akm4531_gain_2dbstep_5bits[(dev->akm_codec.registers[0x3] & 0x1f)]) / 32767.0);

        l >>= 1;
        r >>= 1;

        l *= (((dev->akm_codec.registers[0x0] & 0x80) ? 0 : akm4531_att_2dbstep_5bits[(dev->akm_codec.registers[0x0] & 0x1f) ^ 0x1f]) / 32767.0);
        r *= (((dev->akm_codec.registers[0x1] & 0x80) ? 0 : akm4531_att_2dbstep_5bits[(dev->akm_codec.registers[0x1] & 0x1f) ^ 0x1f]) / 32767.0);
    } else {
        l = (dev->dac[0].out_l * dev->dac[0].vol_l) >> 12;
        l += ((dev->dac[1].out_l * dev->dac[1].vol_l) >> 12);
        r = (dev->dac[0].out_r * dev->dac[0].vol_r) >> 12;
        r += ((dev->dac[1].out_r * dev->dac[1].vol_r) >> 12);

        l >>= 1;
        r >>= 1;

        l = (((l * dev->pcm_vol_l) >> 15) * dev->master_vol_l) >> 15;
        r = (((r * dev->pcm_vol_r) >> 15) * dev->master_vol_r) >> 15;
    }

    if (l < -32768)
        l = -32768;
    else if (l > 32767)
        l = 32767;
    if (r < -32768)
        r = -32768;
    else if (r > 32767)
        r = 32767;

    for (; dev->pos < ((dev->type == AUDIOPCI_ES1370) ? wavetable_pos_global : sound_pos_global); dev->pos++) {
        dev->buffer[dev->pos * 2]     = l;
        dev->buffer[dev->pos * 2 + 1] = r;
    }
}

static void
es137x_poll(void *priv)
{
    es137x_t *dev = (es137x_t *) priv;
    int       frac;
    int       idx;
    int       samp1_l;
    int       samp1_r;
    int       samp2_l;
    int       samp2_r;

    timer_advance_u64(&dev->dac[1].timer, dev->dac[1].latch);

    es137x_scan_fifo(dev);

    es137x_update(dev);

    if (dev->int_ctrl & INT_DAC1_EN) {
        if ((((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880)) && (dev->int_ctrl & INT_DAC1_BYPASS)) || (dev->type == AUDIOPCI_ES1370)) {
            if ((dev->calc_sample_rate_synth != 44100) && (dev->type == AUDIOPCI_ES1370)) {
                if ((dev->dac[0].buffer_pos - dev->dac[0].buffer_pos_end) >= 0 && dev->step_synth >= dev->interp_step_synth)
                    es137x_fetch(dev, 0);

                if (dev->step_synth >= dev->interp_step_synth) {
                    dev->step_synth = 0;
                }

                dev->dac[0].out_l = lerp(dev->dac[0].prev_out_l, dev->dac[0].buffer_l[(dev->dac[0].buffer_pos) & 63], (dev->step_synth + 1) * dev->interp_factor_synth);
                dev->dac[0].out_r = lerp(dev->dac[0].prev_out_r, dev->dac[0].buffer_r[(dev->dac[0].buffer_pos) & 63], (dev->step_synth + 1) * dev->interp_factor_synth);

                dev->step_synth++;
                if (dev->step_synth >= dev->interp_step_synth) {
                    dev->dac[0].prev_out_l = dev->dac[0].out_l;
                    dev->dac[0].prev_out_r = dev->dac[0].out_r;
                    dev->dac[0].buffer_pos++;
                    goto dac0_count;
                }
            } else {
                /* SRC bypass. */
                if ((dev->dac[0].buffer_pos - dev->dac[0].buffer_pos_end) >= 0)
                    es137x_fetch(dev, 0);

                dev->dac[0].out_l = dev->dac[0].buffer_l[dev->dac[0].buffer_pos & 63];
                dev->dac[0].out_r = dev->dac[0].buffer_r[dev->dac[0].buffer_pos & 63];
                dev->dac[0].buffer_pos++;

                goto dac0_count;
            }
        } else {
            frac    = dev->dac[0].ac & 0x7fff;
            idx     = dev->dac[0].ac >> 15;
            samp1_l = dev->dac[0].filtered_l[idx];
            samp1_r = dev->dac[0].filtered_r[idx];
            samp2_l = dev->dac[0].filtered_l[(idx + 1) & 31];
            samp2_r = dev->dac[0].filtered_r[(idx + 1) & 31];

            dev->dac[0].out_l = ((samp1_l * (0x8000 - frac)) + (samp2_l * frac)) >> 15;
            dev->dac[0].out_r = ((samp1_r * (0x8000 - frac)) + (samp2_r * frac)) >> 15;
            dev->dac[0].ac += dev->dac[0].vf;
            dev->dac[0].ac &= ((32 << 15) - 1);
            if ((dev->dac[0].ac >> (15 + 4)) != dev->dac[0].f_pos) {
                es137x_next_sample_filtered(dev, 0, dev->dac[0].f_pos ? 16 : 0);
                dev->dac[0].f_pos = (dev->dac[0].f_pos + 1) & 1;

dac0_count:
                dev->dac[0].curr_samp_ct--;
                if (dev->dac[0].curr_samp_ct < 0) {
                    dev->int_status |= INT_STATUS_DAC1;
                    es137x_update_irqs(dev);
                    dev->dac[0].curr_samp_ct = dev->dac[0].samp_ct;
                }
            }
        }
    }

    if (dev->int_ctrl & INT_DAC2_EN) {
        if ((((dev->type == AUDIOPCI_ES1373) || (dev->type == AUDIOPCI_CT5880)) && (dev->int_ctrl & INT_DAC2_BYPASS)) || (dev->type == AUDIOPCI_ES1370)) {
            if ((dev->calc_sample_rate != 44100) && (dev->type == AUDIOPCI_ES1370)) {
                if ((dev->dac[1].buffer_pos - dev->dac[1].buffer_pos_end) >= 0 && dev->step_pcm >= dev->interp_step)
                    es137x_fetch(dev, 1);

                if (dev->step_pcm >= dev->interp_step) {
                    dev->step_pcm = 0;
                }

                dev->dac[1].out_l = lerp(dev->dac[1].prev_out_l, dev->dac[1].buffer_l[(dev->dac[1].buffer_pos) & 63], (dev->step_pcm + 1) * dev->interp_factor);
                dev->dac[1].out_r = lerp(dev->dac[1].prev_out_r, dev->dac[1].buffer_r[(dev->dac[1].buffer_pos) & 63], (dev->step_pcm + 1) * dev->interp_factor);

                dev->step_pcm++;
                if (dev->step_pcm >= dev->interp_step) {
                    dev->dac[1].prev_out_l = dev->dac[1].out_l;
                    dev->dac[1].prev_out_r = dev->dac[1].out_r;
                    dev->dac[1].buffer_pos++;
                    goto dac1_count;
                }
            } else {
                /* SRC bypass. */
                if ((dev->dac[1].buffer_pos - dev->dac[1].buffer_pos_end) >= 0)
                    es137x_fetch(dev, 1);

                dev->dac[1].out_l = dev->dac[1].buffer_l[dev->dac[1].buffer_pos & 63];
                dev->dac[1].out_r = dev->dac[1].buffer_r[dev->dac[1].buffer_pos & 63];
                dev->dac[1].buffer_pos++;

                goto dac1_count;
            }
        } else {
            frac    = dev->dac[1].ac & 0x7fff;
            idx     = dev->dac[1].ac >> 15;
            samp1_l = dev->dac[1].filtered_l[idx];
            samp1_r = dev->dac[1].filtered_r[idx];
            samp2_l = dev->dac[1].filtered_l[(idx + 1) & 31];
            samp2_r = dev->dac[1].filtered_r[(idx + 1) & 31];

            dev->dac[1].out_l = ((samp1_l * (0x8000 - frac)) + (samp2_l * frac)) >> 15;
            dev->dac[1].out_r = ((samp1_r * (0x8000 - frac)) + (samp2_r * frac)) >> 15;
            dev->dac[1].ac += dev->dac[1].vf;
            dev->dac[1].ac &= ((32 << 15) - 1);
            if ((dev->dac[1].ac >> (15 + 4)) != dev->dac[1].f_pos) {
                es137x_next_sample_filtered(dev, 1, dev->dac[1].f_pos ? 16 : 0);
                dev->dac[1].f_pos = (dev->dac[1].f_pos + 1) & 1;

dac1_count:
                dev->dac[1].curr_samp_ct--;
                if (dev->dac[1].curr_samp_ct < 0) {
                    dev->int_status |= INT_STATUS_DAC2;
                    es137x_update_irqs(dev);
                    dev->dac[1].curr_samp_ct = dev->dac[1].samp_ct;
                }
            }
        }
    }
}

static void
es137x_get_buffer(int32_t *buffer, int len, void *priv)
{
    es137x_t *dev = (es137x_t *) priv;

    es137x_update(dev);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += (dev->buffer[c] / 2);

    dev->pos = 0;
}

static void
es1370_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const es137x_t *dev       = (es137x_t *) priv;
    double          c         = 0.0;
    double          mastervol = ((dev->akm_codec.registers[channel] & 0x80) ? 0 : akm4531_att_2dbstep_5bits[(dev->akm_codec.registers[channel] & 0x1f) ^ 0x1f]) / 32767.0;
    double          cdvol     = ((dev->akm_codec.registers[channel + 0x6] & 0x80) ? 0 : akm4531_gain_2dbstep_5bits[(dev->akm_codec.registers[channel + 0x6] & 0x1f)]) / 32767.0;

    c       = *buffer * mastervol * cdvol;
    *buffer = c;
}

static void
es1371_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const es137x_t *dev = (es137x_t *) priv;
    double          c;
    int             cd     = channel ? dev->cd_vol_r : dev->cd_vol_l;
    int             master = channel ? dev->master_vol_r : dev->master_vol_l;

    c       = ((((*buffer) * cd) / 65536.0) * master) / 65536.0;
    *buffer = c;
}

static inline double
sinc(double x)
{
    return sin(M_PI * x) / (M_PI * x);
}

static void
generate_es137x_filter(void)
{
    /* Cutoff frequency = 1 / 32 */
    float fC = 1.0 / 32.0;
    float gain;
    int   n;

    for (n = 0; n < ES137x_NCoef; n++) {
        /* Blackman window */
        double w = 0.42 - (0.5 * cos((2.0 * n * M_PI) / (double) (ES137x_NCoef - 1))) + (0.08 * cos((4.0 * n * M_PI) / (double) (ES137x_NCoef - 1)));
        /* Sinc filter */
        double h = sinc(2.0 * fC * ((double) n - ((double) (ES137x_NCoef - 1) / 2.0)));

        /* Create windowed-sinc filter */
        low_fir_es137x_coef[n] = w * h;
    }

    low_fir_es137x_coef[(ES137x_NCoef - 1) / 2] = 1.0;

    gain = 0.0;
    for (n = 0; n < ES137x_NCoef; n++)
        gain += low_fir_es137x_coef[n] / (float) N;

    gain /= 0.65;

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < ES137x_NCoef; n++)
        low_fir_es137x_coef[n] /= gain;
}

static void
es137x_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    es137x_t *dev = (es137x_t *) priv;

    for (uint32_t i = 0; i < len; i++)
        es137x_write_fifo(dev, msg[i]);
}

static int
es137x_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    es137x_t *dev = (es137x_t *) priv;
    uint32_t  i   = -1;

    audiopci_log("Abort = %i\n", abort);

    if (dev->uart_status & UART_STATUS_RXRDY)
        abort = 1;

    if (!abort) {
        for (i = 0; i < len; i++) {
            es137x_write_fifo(dev, buffer[i]);
            if (dev->uart_status & UART_STATUS_RXRDY)
                break;
        }
    }

    /* The last sent position is in i. Return 7 - i. */

    return 7 - i;
}

static void es137x_speed_changed(void *priv);

static void *
es1370_init(const device_t *info)
{
    es137x_t *dev = calloc(1, sizeof(es137x_t));
    dev->type = info->local;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, es137x_input_msg, es137x_input_sysex, dev);

    wavetable_add_handler(es137x_get_buffer, dev);
    sound_set_cd_audio_filter(es1370_filter_cd_audio, dev);

    dev->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(dev->gameport, 0x200);

    pci_add_card((info->local & 1) ? PCI_ADD_SOUND : PCI_ADD_NORMAL, es1370_pci_read, es1370_pci_write, dev, &dev->pci_slot);

    timer_add(&dev->dac[1].timer, es137x_poll, dev, 1);

    generate_es137x_filter();

    dev->dac[0].vol_l = 1 << 12;
    dev->dac[0].vol_r = 1 << 12;
    dev->dac[1].vol_l = 1 << 12;
    dev->dac[1].vol_r = 1 << 12;

    dev->pcm_vol_l    = 1 << 15;
    dev->pcm_vol_r    = 1 << 15;
    dev->master_vol_l = 1 << 15;
    dev->master_vol_r = 1 << 15;

    es137x_reset(dev);

    es137x_speed_changed(dev);

    for (int i = 0; i < 0x20; i++) {
        double attn                   = (12.0 - (i * 2.0));
        akm4531_gain_2dbstep_5bits[i] = pow(10, attn / 10.) * 32767.0;
    }

    return dev;
}

static void *
es1371_init(const device_t *info)
{
    es137x_t *dev = calloc(1, sizeof(es137x_t));
    dev->type = info->local & 0xffffff00;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, es137x_input_msg, es137x_input_sysex, dev);

    sound_add_handler(es137x_get_buffer, dev);
    sound_set_cd_audio_filter(es1371_filter_cd_audio, dev);

    dev->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(dev->gameport, 0x200);

    pci_add_card((info->local & 1) ? PCI_ADD_SOUND : PCI_ADD_NORMAL, es1371_pci_read, es1371_pci_write, dev, &dev->pci_slot);

    timer_add(&dev->dac[1].timer, es137x_poll, dev, 1);

    generate_es137x_filter();

    ac97_codec       = &dev->codec;
    ac97_codec_count = 1;
    ac97_codec_id    = 0;
    /* Let the machine decide the codec on onboard implementations. */
    if (!(info->local & 1))
        device_add(ac97_codec_get(device_get_config_int("codec")));

    es137x_reset(dev);

    return dev;
}

static void
es137x_close(void *priv)
{
    es137x_t *dev = (es137x_t *) priv;

    free(dev);
}

static void
es137x_speed_changed(void *priv)
{
    es137x_t *dev = (es137x_t *) priv;

    dev->dac[1].latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / (double) ((dev->type == AUDIOPCI_ES1370) ? WT_FREQ : SOUND_FREQ)));
}

static const device_config_t es1370_config[] = {
    // clang-format off
    {
        .name           = "receive_input",
        .description    = "Receive input (MIDI)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t es1371_config[] = {
    // clang-format off
    {
        .name           = "codec",
        .description    = "Codec",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = AC97_CODEC_TR28023,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Asahi Kasei AK4540",                .value = AC97_CODEC_AK4540  },
            { .description = "TriTech TR28023 / Creative CT1297", .value = AC97_CODEC_TR28023 },
            { .description = ""                                                               }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t es1373_config[] = {
    // clang-format off
    {
        .name           = "codec",
        .description    = "Codec",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = AC97_CODEC_CS4297A,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Crystal CS4297A",                   .value = AC97_CODEC_CS4297A  },
            { .description = "SigmaTel STAC9721T",                .value = AC97_CODEC_STAC9721 },
            { .description = "TriTech TR28023 / Creative CT1297", .value = AC97_CODEC_TR28023  },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t ct5880_config[] = {
    // clang-format off
    {
        .name           = "codec",
        .description    = "Codec",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = AC97_CODEC_STAC9708,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "SigmaTel STAC9708T",                .value = AC97_CODEC_STAC9708 },
            { .description = "SigmaTel STAC9721T (stereo)",       .value = AC97_CODEC_STAC9721 },
            { .description = "TriTech TR28023 / Creative CT1297", .value = AC97_CODEC_TR28023  },
            { .description = ""                                                                }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t es1371_onboard_config[] = {
    // clang-format off
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t es1370_device = {
    .name          = "Ensoniq AudioPCI (ES1370)",
    .internal_name = "es1370",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_ES1370,
    .init          = es1370_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = es1370_config
};

const device_t es1371_device = {
    .name          = "Ensoniq AudioPCI (ES1371)",
    .internal_name = "es1371",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_ES1371,
    .init          = es1371_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = es1371_config
};

const device_t es1371_onboard_device = {
    .name          = "Ensoniq AudioPCI (ES1371) (On-Board)",
    .internal_name = "es1371_onboard",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_ES1371 | 1,
    .init          = es1371_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = es1371_onboard_config
};

const device_t es1373_device = {
    .name          = "Sound Blaster PCI 128 (ES1373)",
    .internal_name = "es1373",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_ES1373,
    .init          = es1371_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = es1373_config
};

const device_t es1373_onboard_device = {
    .name          = "Sound Blaster PCI 128 (ES1373) (On-Board)",
    .internal_name = "es1373_onboard",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_ES1373 | 1,
    .init          = es1371_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = es1371_onboard_config
};

const device_t ct5880_device = {
    .name          = "Sound Blaster PCI 4.1 (CT5880)",
    .internal_name = "ct5880",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_CT5880,
    .init          = es1371_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = ct5880_config
};

const device_t ct5880_onboard_device = {
    .name          = "Sound Blaster PCI 4.1 (CT5880) (On-Board)",
    .internal_name = "ct5880_onboard",
    .flags         = DEVICE_PCI,
    .local         = AUDIOPCI_CT5880 | 1,
    .init          = es1371_init,
    .close         = es137x_close,
    .reset         = es137x_reset,
    .available     = NULL,
    .speed_changed = es137x_speed_changed,
    .force_redraw  = NULL,
    .config        = es1371_onboard_config
};
