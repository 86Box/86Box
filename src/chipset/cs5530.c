/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Cyrix Cx5530 / National Geode CS5530 I/O
 *          companion.
 *
 *          A PCI device of five functions and a USB controller:
 *          - F0, the PCI-to-ISA bridge, its power management, traps and
 *            timers;
 *          - F1, the SMI status and the ACPI timer, at F1BAR;
 *          - F2, the IDE controllers, the bus masters and timings at F2BAR;
 *          - F3, XpressAUDIO: the AC97 codec interface, the audio bus
 *            masters and the I/O traps VSA emulates a Sound Blaster with;
 *          - F4, the display interface: the dot clock PLL, the DACs and
 *            the gamma RAM of the MediaGX's display controller.
 *          Almost every event can be an SMI, reported in two or three
 *          levels; SMI# stays asserted while the top level has a bit set,
 *          and an RSM with SMI# still low takes the next SMI at once.
 *
 *          CS5530 data book, revision 4.1 (April 2000); the Cx5530 is the
 *          same part.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/usb.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_ac97.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>
#include <86box/cyrix_gxm.h>

#ifdef ENABLE_CS5530_LOG
int cs5530_do_log = ENABLE_CS5530_LOG;

static void
cs5530_log(const char *fmt, ...)
{
    va_list ap;

    if (cs5530_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cs5530_log(fmt, ...)
#endif

/* Top level SMI status, F1BAR+00h/02h (Table 4-16). */
#define TOP_PM          (1 << 0)
#define TOP_AUDIO       (1 << 1)
#define TOP_RETRACE     (1 << 5)
#define TOP_VGA_TIMER   (1 << 6)
#define TOP_A20M        (1 << 7)
#define TOP_SOFTWARE    (1 << 8)
#define TOP_TRAPS       (1 << 9)
#define TOP_USB         (1 << 14)
#define TOP_SUSP_MOD    (1 << 15)

/* Second level general traps and timers, F1BAR+04h/06h. */
#define GEN_GPT1        (1 << 0)
#define GEN_GPT2        (1 << 1)
#define GEN_UDEF1       (1 << 2)
#define GEN_PCI_TRAP    (1 << 5)

/* Second level audio, F3BAR+10h/12h; the bus masters' are 2-7. */
#define AUD_IO_TRAP     (1 << 0)
#define AUD_BM(n)       (4 << (n))

/* The audio bus masters' PRD flags (Table 3-64). */
#define PRD_EOT         (1U << 31)
#define PRD_EOP         (1U << 30)
#define PRD_JMP         (1U << 29)

/* I/O trap status, F3BAR+14h. */
#define IOT_WRITE       (1 << 14)
#define IOT_SB_FAST     (1 << 13)
#define IOT_DMA         (1 << 12)
#define IOT_MPU         (1 << 11)
#define IOT_SB          (1 << 10)

/* The device classes the power management traps and idle timers watch. */
enum {
    PM_HDD_PRI = 0,
    PM_FDD,
    PM_SERPAR,
    PM_KBD,
    PM_UDEF1,
    PM_UDEF2,
    PM_UDEF3,
    PM_HDD_SEC,
    PM_GAME,
    PM_DEVICES
};

/* The audio traps (F3BAR+18h). */
enum {
    AT_SB = 0,
    AT_FM,
    AT_MPU_LOW,
    AT_MPU_HIGH,
    AT_DMA_LOW,
    AT_DMA_HIGH,
    AT_TRAPS
};

typedef struct cs5530_t {
    uint8_t  pci_conf[5][256];
    uint8_t  usb_conf[256];
    uint8_t  pci_slot;
    uint8_t  usb_slot;

    /* F1. */
    uint16_t top_smi;
    uint16_t gen_smi;
    uint8_t  pm_smi[4]; /* F0 F4h-F7h, mirrored at 84h-87h */
    int      smi_level;
    mem_mapping_t f1_mapping;

    /* F2. */
    uint32_t ide_timing[8];
    uint16_t f2_base;
    sff8038i_t *bm[2];

    /* F3. */
    uint32_t codec_gpio_status;
    uint32_t codec_gpio_control;
    uint32_t codec_status;
    uint32_t codec_command;
    uint16_t audio_smi;
    uint32_t io_trap_status;
    uint16_t io_trap_enable;
    uint16_t irq_enable;
    uint16_t irq_control;
    uint16_t irq_mask;
    uint16_t irq_level;
    uint8_t  abm_cmd[6];
    uint8_t  abm_smi[6];
    uint32_t abm_prd[6];
    ac97_codec_t *codec;

    /* The audio bus masters' engines: the region of the PRD being moved, a
       frame (32 bits on 0 and 1, 16 on the others) each codec sample. */
    struct {
        uint32_t addr;
        uint32_t count;
        uint32_t flags;
        int      loaded;
    } abm[6];
    pc_timer_t abm_timer;
    int32_t    abm_out_l;
    int32_t    abm_out_r;
    int32_t    abm_buffer[SOUNDBUFLEN * 2];
    int        abm_pos;
    mem_mapping_t f3_mapping;
    void    *audio_trap[AT_TRAPS];

    /* The board's MIDI UART, at the Sound Blaster base (see
       cs5530_midi_uart_read()). */
    uint16_t midi_base;
    uint8_t  midi_ier;
    uint8_t  midi_lcr;
    uint8_t  midi_mcr;
    uint8_t  midi_scr;
    uint16_t midi_dl;
    uint8_t  midi_rx[256];
    uint8_t  midi_rx_r;
    uint8_t  midi_rx_w;
    int      midi_rx_count;
    int      midi_sysex;

    /* F4. */
    uint32_t f4_regs[11];
    uint8_t  gamma_addr;
    uint32_t gamma[256];
    mem_mapping_t f4_mapping;

    /* The power management device traps and idle timers. */
    void    *pm_trap[PM_DEVICES][6];
    uint16_t idle_count[PM_DEVICES];

    /* Timers: 1 ms for the general purpose and idle timers, and the VGA
       timer's own. */
    pc_timer_t ms_timer;
    pc_timer_t vga_timer;
    pc_timer_t acpi_timer;
    int        ms_count;
    uint16_t   gpt_count[2];
    uint8_t    vga_count;

    usb_t     *usb;
    port_92_t *port_92;
    uint8_t    usb_irq_state;
    uint8_t    usb_smi_level;
    uint8_t    a20m;
} cs5530_t;

static cs5530_t *cs5530 = NULL;

extern double cpuclock;

static void cs5530_pm_traps_recalc(cs5530_t *dev);

/* ----------------------------------------------------------------------
   SMI#. It is low while the top level status has a bit set, or while the
   USB controller's SMI is asserted with 42h[7] routing it to the pin (Table
   4-2, F0 Index 42h); an RSM with it still low is the next SMI (MediaGX
   data book, 3.11.3).
   ---------------------------------------------------------------------- */
static void
cs5530_smi_update(cs5530_t *dev)
{
    const int level = !!(dev->top_smi & ~TOP_SUSP_MOD) ||
                      (dev->usb_smi_level && (dev->pci_conf[0][0x42] & 0x80));

    if (level && !dev->smi_level)
        cyrix_gxm_smi_pin();
    dev->smi_level = level;
}

static int
cs5530_smi_pin(void)
{
    return (cs5530 != NULL) && cs5530->smi_level;
}

static void
cs5530_smi(cs5530_t *dev, uint16_t top)
{
    cs5530_log("CS5530: SMI %04X\n", top);

    dev->top_smi |= top;
    cs5530_smi_update(dev);
}

/* The MediaGX's serial packet (PSERIAL, 3.1.2): its Video IRQ bit is the
   video retrace SMI, when 83h[2] enables it. */
static void
cs5530_serial_packet(uint8_t packet)
{
    if ((cs5530 != NULL) && (packet & 0x80) && (cs5530->pci_conf[0][0x83] & 0x04))
        cs5530_smi(cs5530, TOP_RETRACE);
}

static void
cs5530_pm_smi(cs5530_t *dev, int reg, uint8_t bit)
{
    dev->pm_smi[reg] |= bit;
    cs5530_smi(dev, TOP_PM);
}

static void
cs5530_gen_smi(cs5530_t *dev, uint16_t bit)
{
    dev->gen_smi |= bit;
    cs5530_smi(dev, TOP_TRAPS);
}

/* ----------------------------------------------------------------------
   A20M#: the keyboard controller's and port 92h's requests are not wired to
   the processor; the change is an SMI, and the handler sets the MediaGX's
   A20M bit (F0 53h[0], F1BAR+00h[7]).
   ---------------------------------------------------------------------- */
static void
cs5530_a20_trap(int state, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if (dev->a20m == !state)
        return;
    dev->a20m = !state;

    if (dev->pci_conf[0][0x53] & 0x01)
        cs5530_smi(dev, TOP_A20M);
}

/* ----------------------------------------------------------------------
   The power management traps and idle timers (3.4.3.1). An access to a
   device both raises its trap's SMI, if enabled, and reloads its idle
   timer; the device sees the access as it would.
   ---------------------------------------------------------------------- */
static void
cs5530_pm_access(cs5530_t *dev, int devc, int write)
{
    static const uint8_t trap_reg[PM_DEVICES]  = { 2, 2, 2, 2, 0xff, 0xff, 0xff, 2, 0 };
    static const uint8_t trap_bit[PM_DEVICES]  = { 0x01, 0x02, 0x04, 0x08, 0, 0, 0, 0x20, 0x10 };
    static const uint8_t timer_reg[PM_DEVICES] = { 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x83, 0 };
    static const uint8_t timer_bit[PM_DEVICES] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0 };
    const uint8_t       *conf                  = dev->pci_conf[0];

    /* Idle timers: reload. */
    if (timer_bit[devc] && (conf[timer_reg[devc]] & timer_bit[devc])) {
        static const uint8_t count_reg[PM_DEVICES] = { 0x98, 0x9a, 0x9c, 0x9e, 0xa0, 0xa2, 0xa4, 0xac, 0 };

        dev->idle_count[devc] = conf[count_reg[devc]] | (conf[count_reg[devc] + 1] << 8);
    }

    /* GP Timer 1 is re-triggered by the devices of 89h[6:0] and 8Bh[7]. */
    if (devc <= PM_UDEF3) {
        if ((conf[0x83] & 0x01) && (conf[0x89] & (1 << devc)))
            dev->gpt_count[0] = (conf[0x8b] & 0x10) ? (conf[0x88] << 8) : conf[0x88];
    } else if ((devc == PM_HDD_SEC) && (conf[0x83] & 0x01) && (conf[0x8b] & 0x80))
        dev->gpt_count[0] = (conf[0x8b] & 0x10) ? (conf[0x88] << 8) : conf[0x88];

    if (!(conf[0x80] & 0x01))
        return;

    switch (devc) {
        case PM_UDEF1:
        case PM_UDEF2:
        case PM_UDEF3:
            if ((conf[0x80] & 0x04) && (conf[0x82] & (0x10 << (devc - PM_UDEF1))))
                cs5530_gen_smi(dev, GEN_UDEF1 << (devc - PM_UDEF1));
            break;
        case PM_GAME:
            if ((write && (conf[0x53] & 0x08)) || (!write && (conf[0x83] & 0x10)))
                cs5530_pm_smi(dev, 0, 0x10);
            break;
        case PM_HDD_SEC:
            if ((conf[0x80] & 0x04) && (conf[0x83] & 0x40))
                cs5530_pm_smi(dev, 2, trap_bit[devc]);
            break;

        default:
            if ((conf[0x80] & 0x04) && (conf[0x82] & trap_bit[devc]))
                cs5530_pm_smi(dev, trap_reg[devc], trap_bit[devc]);
            break;
    }
}

static void
cs5530_pm_trap(uint16_t size, uint16_t port, uint8_t write, UNUSED(uint8_t val), void *priv)
{
    int devc = (int) (intptr_t) priv;

    if (cs5530 == NULL)
        return;

    /* Partial hard disk decode: writes to 1F6h/1F7h (176h/177h) alone. */
    if ((devc == PM_HDD_PRI) && (cs5530->pci_conf[0][0x93] & 0x20) && (!write || ((port & 0x3fe) != 0x1f6)))
        return;
    if ((devc == PM_HDD_SEC) && (cs5530->pci_conf[0][0x93] & 0x10) && (!write || ((port & 0x3fe) != 0x176)))
        return;
    /* Writes to 3F7h/377h are not the hard disk's. */
    if (((devc == PM_HDD_PRI) || (devc == PM_HDD_SEC)) && write && ((port & 0x37f) == 0x377))
        return;

    (void) size;
    cs5530_pm_access(cs5530, devc, write);
}

static void
cs5530_udef_trap(uint16_t size, uint16_t port, uint8_t write, UNUSED(uint8_t val), void *priv)
{
    const int devc = (int) (intptr_t) priv;
    uint8_t   ctl;

    if (cs5530 == NULL)
        return;

    ctl = cs5530->pci_conf[0][0xcc + (devc - PM_UDEF1)];
    /* Bits 6 and 5 track writes and reads. */
    if (!(ctl & (write ? 0x40 : 0x20)))
        return;

    (void) size;
    (void) port;
    cs5530_pm_access(cs5530, devc, write);
}

static void
cs5530_pm_trap_set(cs5530_t *dev, int devc, int slot, int enable, uint16_t base, uint16_t size)
{
    if (dev->pm_trap[devc][slot] == NULL)
        dev->pm_trap[devc][slot] = io_trap_add(((devc >= PM_UDEF1) && (devc <= PM_UDEF3)) ? cs5530_udef_trap : cs5530_pm_trap,
                                               (void *) (intptr_t) devc);
    io_trap_remap(dev->pm_trap[devc][slot], enable, base, size);
}

/* The traps are on while either the trap or the idle timer (or the GP Timer
   1 re-trigger) of the device wants its accesses. */
static void
cs5530_pm_traps_recalc(cs5530_t *dev)
{
    const uint8_t *conf = dev->pci_conf[0];
    const int      pm   = conf[0x80] & 0x01;
    const int      trap = pm && (conf[0x80] & 0x04);
    const int      gpt1 = conf[0x83] & 0x01;
    int            on;
    int            com_kbd;
    uint16_t       fdc;

    /* Primary hard disk. */
    on = (trap && (conf[0x82] & 0x01)) || (conf[0x81] & 0x01) || (gpt1 && (conf[0x89] & 0x01));
    cs5530_pm_trap_set(dev, PM_HDD_PRI, 0, on, 0x1f0, 8);
    cs5530_pm_trap_set(dev, PM_HDD_PRI, 1, on, 0x3f6, 2);

    /* Secondary hard disk. */
    on = (trap && (conf[0x83] & 0x40)) || (conf[0x83] & 0x80) || (gpt1 && (conf[0x8b] & 0x80));
    cs5530_pm_trap_set(dev, PM_HDD_SEC, 0, on, 0x170, 8);
    cs5530_pm_trap_set(dev, PM_HDD_SEC, 1, on, 0x376, 2);

    /* Floppy: the primary or secondary controller (93h[7]). */
    on  = (trap && (conf[0x82] & 0x02)) || (conf[0x81] & 0x02) || (gpt1 && (conf[0x89] & 0x02));
    fdc = (conf[0x93] & 0x80) ? 0x3f0 : 0x370;
    cs5530_pm_trap_set(dev, PM_FDD, 0, on, fdc + 2, 4);
    cs5530_pm_trap_set(dev, PM_FDD, 1, on, fdc + 7, 1);

    /* Keyboard and mouse, with the serial mouse's port (93h[1:0]). */
    com_kbd = (conf[0x93] & 0x02) ? ((conf[0x93] & 0x01) ? 2 : 1) : 0;
    on      = (trap && (conf[0x82] & 0x08)) || (conf[0x81] & 0x08) || (gpt1 && (conf[0x89] & 0x08));
    cs5530_pm_trap_set(dev, PM_KBD, 0, on, 0x60, 1);
    cs5530_pm_trap_set(dev, PM_KBD, 1, on, 0x64, 1);
    cs5530_pm_trap_set(dev, PM_KBD, 2, on && com_kbd, (com_kbd == 2) ? 0x2f8 : 0x3f8, 8);

    /* Parallel and serial ports, less the mouse's. */
    on = (trap && (conf[0x82] & 0x04)) || (conf[0x81] & 0x04) || (gpt1 && (conf[0x89] & 0x04));
    cs5530_pm_trap_set(dev, PM_SERPAR, 0, on, 0x378, 8);
    cs5530_pm_trap_set(dev, PM_SERPAR, 1, on, 0x278, 8);
    cs5530_pm_trap_set(dev, PM_SERPAR, 2, on && (com_kbd != 1), 0x3f8, 8);
    cs5530_pm_trap_set(dev, PM_SERPAR, 3, on && (com_kbd != 2), 0x2f8, 8);
    cs5530_pm_trap_set(dev, PM_SERPAR, 4, on, 0x3e8, 8);
    cs5530_pm_trap_set(dev, PM_SERPAR, 5, on, 0x2e8, 8);

    /* The user defined devices, the I/O mapped ones (C0h-CEh). */
    for (int i = 0; i < 3; i++) {
        const uint8_t  ctl  = conf[0xcc + i];
        const uint16_t base = conf[0xc0 + (i << 2)] | (conf[0xc1 + (i << 2)] << 8);
        const int      mask = ctl & 0x1f;
        const int      devc = PM_UDEF1 + i;

        on = (trap && (conf[0x82] & (0x10 << i))) || (conf[0x81] & (0x10 << i)) || (gpt1 && (conf[0x89] & (0x10 << i)));
        on = on && !(ctl & 0x80);
        cs5530_pm_trap_set(dev, devc, 0, on, base & ~mask, mask + 1);
    }

    /* The game port, for its SMIs (53h[3], 83h[4]). */
    on = pm && ((conf[0x53] & 0x08) || (conf[0x83] & 0x10));
    cs5530_pm_trap_set(dev, PM_GAME, 0, on, 0x200, 2);
}

/* The idle timers count seconds, the general purpose ones seconds or
   milliseconds; the timer runs while any is on. */
static void
cs5530_ms_timer(void *priv)
{
    static const uint8_t timer_reg[PM_DEVICES] = { 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x83, 0 };
    static const uint8_t timer_bit[PM_DEVICES] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0 };
    static const uint8_t smi_bit[PM_DEVICES]   = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x10, 0 };
    cs5530_t            *dev                   = (cs5530_t *) priv;
    const uint8_t       *conf                  = dev->pci_conf[0];
    const int            second                = (++dev->ms_count >= 1000);

    if (second)
        dev->ms_count = 0;

    /* GP Timer 1: 89h[7] picks 1 ms; GP Timer 2: 8Bh[3]. */
    if ((conf[0x83] & 0x01) && dev->gpt_count[0] && (second || (conf[0x89] & 0x80))) {
        if (!--dev->gpt_count[0])
            cs5530_gen_smi(dev, GEN_GPT1);
    }
    if ((conf[0x83] & 0x02) && dev->gpt_count[1] && (second || (conf[0x8b] & 0x08))) {
        if (!--dev->gpt_count[1])
            cs5530_gen_smi(dev, GEN_GPT2);
    }

    if (second && (conf[0x80] & 0x02)) {
        for (int i = 0; i < PM_DEVICES; i++) {
            if (timer_bit[i] && (conf[timer_reg[i]] & timer_bit[i]) && dev->idle_count[i]) {
                if (!--dev->idle_count[i])
                    cs5530_pm_smi(dev, (i == PM_HDD_SEC) ? 2 : 1, smi_bit[i]);
            }
        }
    }

    timer_advance_u64(&dev->ms_timer, TIMER_USEC * 1000);
}

static void
cs5530_ms_timer_recalc(cs5530_t *dev)
{
    const uint8_t *conf = dev->pci_conf[0];
    const int      on   = (conf[0x83] & 0x03) || ((conf[0x80] & 0x02) && ((conf[0x81] & 0xff) || (conf[0x83] & 0x80)));

    if (on && !timer_is_enabled(&dev->ms_timer))
        timer_set_delay_u64(&dev->ms_timer, TIMER_USEC * 1000);
    else if (!on && timer_is_enabled(&dev->ms_timer))
        timer_disable(&dev->ms_timer);
}

/* The VGA Timer (8Eh), 1 ms or 32 us a count (8Bh[6]). The count goes down
   with each clock of the timebase, which loading the count does not restart:
   VSA turns the timer off and on in its SMIs, and SoftVGA's chunked text
   redraw, which goes on from this SMI, starved while the guest polled a
   trapped VGA port. */
static void
cs5530_vga_timer(void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    /* The clock stops at its first tick with the timer off, so an off and
       on in one SMI leaves it in step. */
    if (!(dev->pci_conf[0][0x83] & 0x08))
        return;

    if (dev->vga_count && !--dev->vga_count)
        cs5530_smi(dev, TOP_VGA_TIMER);

    timer_advance_u64(&dev->vga_timer, (dev->pci_conf[0][0x8b] & 0x40) ? (TIMER_USEC * 32) : (TIMER_USEC * 1000));
}

static void
cs5530_vga_timer_load(cs5530_t *dev)
{
    dev->vga_count = dev->pci_conf[0][0x8e];
    if ((dev->pci_conf[0][0x83] & 0x08) && !timer_is_enabled(&dev->vga_timer))
        timer_set_delay_u64(&dev->vga_timer, (dev->pci_conf[0][0x8b] & 0x40) ? (TIMER_USEC * 32) : (TIMER_USEC * 1000));
}

/* The ACPI timer: 24 bits at 3.579545 MHz, an SMI when its MSB toggles
   (83h[5]). */
static uint32_t
cs5530_acpi_count(void)
{
    return (uint32_t) ((((double) tsc) * ((double) ACPI_TIMER_FREQ)) / ((double) cpuclock)) & 0x00ffffff;
}

static void
cs5530_acpi_timer(void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if ((dev->pci_conf[0][0x80] & 0x01) && (dev->pci_conf[0][0x83] & 0x20))
        cs5530_pm_smi(dev, 3, 0x01);

    timer_advance_u64(&dev->acpi_timer, (uint64_t) ((double) TIMER_USEC * 8388608.0 * 1000000.0 / ACPI_TIMER_FREQ));
}

static uint8_t
cs5530_acpi_port_read(uint16_t port, UNUSED(void *priv))
{
    return cs5530_acpi_count() >> ((port & 3) << 3);
}

static uint32_t
cs5530_acpi_port_readl(UNUSED(uint16_t port), UNUSED(void *priv))
{
    return cs5530_acpi_count();
}

/* ----------------------------------------------------------------------
   F1BAR: the SMI status and the ACPI timer, 256 bytes; 50h-FFh are F0's
   registers.
   ---------------------------------------------------------------------- */
static void cs5530_f0_write(cs5530_t *dev, int addr, uint8_t val);

static uint8_t
cs5530_f1_read(uint32_t addr, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    uint8_t   ret = 0x00;

    addr &= 0xff;

    switch (addr) {
        case 0x00:
        case 0x01:
            ret = (dev->top_smi | ((dev->pci_conf[0][0x96] & 0x01) ? TOP_SUSP_MOD : 0)) >> ((addr & 1) << 3);
            break;
        case 0x02:
        case 0x03:
            ret = (dev->top_smi | ((dev->pci_conf[0][0x96] & 0x01) ? TOP_SUSP_MOD : 0)) >> ((addr & 1) << 3);
            /* Reading clears the status. */
            if (addr == 0x03)
                dev->top_smi &= 0x00ff;
            else
                dev->top_smi &= 0xff00;
            cs5530_smi_update(dev);
            break;
        case 0x04:
        case 0x05:
            ret = dev->gen_smi >> ((addr & 1) << 3);
            break;
        case 0x06:
        case 0x07:
            ret = dev->gen_smi >> ((addr & 1) << 3);
            dev->gen_smi &= (addr == 0x07) ? 0x00ff : 0xff00;
            break;
        case 0x1c ... 0x1f:
            ret = cs5530_acpi_count() >> ((addr & 3) << 3);
            break;
        case 0x50 ... 0xff:
            ret = dev->pci_conf[0][addr];
            break;

        default:
            break;
    }


    return ret;
}

static uint16_t
cs5530_f1_readw(uint32_t addr, void *priv)
{
    return cs5530_f1_read(addr, priv) | (cs5530_f1_read(addr + 1, priv) << 8);
}

static uint32_t
cs5530_f1_readl(uint32_t addr, void *priv)
{
    return cs5530_f1_readw(addr, priv) | (cs5530_f1_readw(addr + 2, priv) << 16);
}

static void
cs5530_f1_write(uint32_t addr, uint8_t val, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    addr &= 0xff;
    if (addr >= 0x50)
        cs5530_f0_write(dev, addr, val);
}

static void
cs5530_f1_writew(uint32_t addr, uint16_t val, void *priv)
{
    cs5530_f1_write(addr, val, priv);
    cs5530_f1_write(addr + 1, val >> 8, priv);
}

static void
cs5530_f1_writel(uint32_t addr, uint32_t val, void *priv)
{
    cs5530_f1_writew(addr, val, priv);
    cs5530_f1_writew(addr + 2, val >> 16, priv);
}

/* ----------------------------------------------------------------------
   F3: XpressAUDIO (3.7, Table 4-20).
   ---------------------------------------------------------------------- */
static void
cs5530_internal_irq_update(cs5530_t *dev)
{
    const uint16_t level = dev->irq_enable & dev->irq_control;

    for (int i = 3; i < 16; i++) {
        const uint16_t bit = 1 << i;

        if ((level ^ dev->irq_level) & bit) {
            if (level & bit)
                picint(bit);
            else
                picintc(bit);
        }
    }
    dev->irq_level = level;
}

/* The audio bus masters (3.7.1.1-3.7.1.4). 0, 2 and 4 read memory to the
   codec, 1, 3 and 5 write what it captures (silence here); 0 and 1 move a
   32-bit frame, left and right, the others 16 bits. */
static void
cs5530_abm_update_buffer(cs5530_t *dev)
{
    int l;
    int r;
    int master_l;
    int master_r;
    int pcm_l;
    int pcm_r;

    ac97_codec_getattn(dev->codec, 0x02, &master_l, &master_r);
    ac97_codec_getattn(dev->codec, 0x18, &pcm_l, &pcm_r);

    l = (int) ((((((int64_t) dev->abm_out_l) * pcm_l) >> 15) * master_l) >> 15);
    r = (int) ((((((int64_t) dev->abm_out_r) * pcm_r) >> 15) * master_r) >> 15);
    if (l < -32768)
        l = -32768;
    else if (l > 32767)
        l = 32767;
    if (r < -32768)
        r = -32768;
    else if (r > 32767)
        r = 32767;

    for (; dev->abm_pos < sound_pos_global; dev->abm_pos++) {
        dev->abm_buffer[dev->abm_pos * 2]     = l;
        dev->abm_buffer[dev->abm_pos * 2 + 1] = r;
    }
}

static void
cs5530_abm_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    cs5530_abm_update_buffer(dev);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += dev->abm_buffer[c];

    dev->abm_pos = 0;
}

/* The next PRD, through its table address, which then points past it. A JMP
   moves no data: the table goes on at its base address. */
static void
cs5530_abm_load(cs5530_t *dev, int bm)
{
    uint32_t prd[2];

    dev->abm[bm].loaded = 0;
    for (int jumps = 0; jumps < 16; jumps++) {
        dma_bm_read(dev->abm_prd[bm], (uint8_t *) prd, 8, 4);
        if (prd[1] & PRD_JMP) {
            dev->abm_prd[bm] = prd[0] & 0xfffffffc;
            continue;
        }

        dev->abm_prd[bm] += 8;
        dev->abm[bm].addr   = prd[0] & 0xfffffffe;
        dev->abm[bm].count  = (prd[1] & 0xfffe) ? (prd[1] & 0xfffe) : 0x10000;
        dev->abm[bm].flags  = prd[1];
        dev->abm[bm].loaded = 1;
        return;
    }
}

static void
cs5530_abm_frame(cs5530_t *dev, int bm)
{
    const uint32_t size = (bm < 2) ? 4 : 2;
    uint32_t       frame = 0;

    /* Off, out of PRDs, or paused on a second EOP until 21h is read. */
    if (!(dev->abm_cmd[bm] & 0x01) || !dev->abm[bm].loaded || (dev->abm_smi[bm] & 0x02))
        return;

    if (dev->abm_cmd[bm] & 0x08)
        dma_bm_write(dev->abm[bm].addr, (uint8_t *) &frame, size, size);
    else {
        dma_bm_read(dev->abm[bm].addr, (uint8_t *) &frame, size, size);
        if (bm == 0) {
            /* Slot 3, left, is the high word: the frame goes out on the
               AC-link MSB first (VSA puts an SB16's left sample there). */
            dev->abm_out_l = (int16_t) (frame >> 16);
            dev->abm_out_r = (int16_t) (frame & 0xffff);
        }
    }

    dev->abm[bm].addr += size;
    dev->abm[bm].count = (dev->abm[bm].count > size) ? (dev->abm[bm].count - size) : 0;
    if (dev->abm[bm].count)
        return;

    if (dev->abm[bm].flags & PRD_EOP) {
        if (dev->abm_smi[bm] & 0x01)
            dev->abm_smi[bm] |= 0x02;
        else {
            dev->abm_smi[bm] |= 0x01;
            cs5530_smi(dev, TOP_AUDIO);
        }
    }

    if (dev->abm[bm].flags & PRD_EOT)
        dev->abm[bm].loaded = 0;
    else
        cs5530_abm_load(dev, bm);
}

static void
cs5530_abm_timer(void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    double    freq;
    int       on = 0;

    cs5530_abm_update_buffer(dev);

    for (int bm = 0; bm < 6; bm++) {
        cs5530_abm_frame(dev, bm);
        on |= dev->abm_cmd[bm] & 0x01;
    }

    if (!(dev->abm_cmd[0] & 0x01) || !dev->abm[0].loaded)
        dev->abm_out_l = dev->abm_out_r = 0;

    if (on) {
        freq = (double) ac97_codec_getrate(dev->codec, 0x2c);
        if (freq < 1.0)
            freq = (double) SOUND_FREQ;
        timer_advance_u64(&dev->abm_timer, (uint64_t) ((double) TIMER_USEC * (1000000.0 / freq)));
    }
}

/* A command register write: enabling loads the first PRD. */
static void
cs5530_abm_command(cs5530_t *dev, int bm, uint8_t val)
{
    const uint8_t old = dev->abm_cmd[bm];

    dev->abm_cmd[bm] = val & 0x09;

    if ((val & 0x01) && !(old & 0x01)) {
        cs5530_abm_load(dev, bm);
        if (!timer_is_on(&dev->abm_timer))
            timer_set_delay_u64(&dev->abm_timer, TIMER_USEC);
    } else if (!(val & 0x01))
        dev->abm[bm].loaded = 0;
}

/* The audio I/O traps: an SMI, the cycle's address and direction in
   F3BAR+14h. With fast path write, a write to the even ports of the Sound
   Blaster and the FM chip is latched without an SMI, and the odd one takes
   it with both bytes; with fast path read, the status reads answer from
   F3BAR+18h[10:9] without one. */
static void
cs5530_audio_trap(uint16_t size, uint16_t port, uint8_t write, uint8_t val, void *priv)
{
    const int trap = (int) (intptr_t) priv;
    uint32_t  status;
    int       fast = 0;

    if ((cs5530 == NULL) || in_smm)
        return;

    status = cs5530->io_trap_status & 0xffff0000;

    if ((trap == AT_SB) || (trap == AT_FM)) {
        const uint16_t reg = (trap == AT_FM) ? (port & 3) : (port & 0x0f);

        /* Fast path read: 388h-38Bh, 2x0h-2x3h, 2x8h and 2x9h are answered
           by cs5530_audio_fast_read(), with no SMI. */
        if (!write && (cs5530->io_trap_enable & 0x0010) && ((trap == AT_FM) || (reg <= 3) || (reg == 8) || (reg == 9)))
            return;

        /* Fast path write: the FM pairs 388h/389h and 38Ah/38Bh and the
           Sound Blaster's 2x0h-2x3h and 2x8h/2x9h. The data book lists 388h,
           38Ah and 38Bh alone, but 389h is the data port of 388h's index:
           VSA takes the OPL register from the even write's latch (31:24).
           Bit 15 is A1 of the last fast path write. */
        status |= cs5530->io_trap_status & 0x8000;
        if (write && (cs5530->io_trap_enable & 0x0800) &&
            ((trap == AT_FM) || (reg <= 3) || (reg == 8) || (reg == 9))) {
            status = (status & ~0x8000) | ((port & 2) ? 0x8000 : 0);
            if (!(port & 1)) {
                cs5530->io_trap_status = (status & 0x00ffffff) | ((uint32_t) val << 24) | (cs5530->io_trap_status & 0x7fff);
                return;
            }
            fast = 1;
        }
        if (write)
            status = (status & 0xff00ffff) | ((uint32_t) val << 16);

        status |= fast ? IOT_SB_FAST : IOT_SB;
    } else if ((trap == AT_MPU_LOW) || (trap == AT_MPU_HIGH))
        status |= IOT_MPU;
    else
        status |= IOT_DMA;

    if (write)
        status |= IOT_WRITE;
    status |= port & 0x3ff;

    cs5530->io_trap_status = status;
    cs5530->audio_smi |= AUD_IO_TRAP;
    cs5530_smi(cs5530, TOP_AUDIO);

    (void) size;
}

/* A trapped DMA controller access: the SMI, and the cycle goes nowhere; the
   handler repeats it, from SMM, where it is not trapped. */
static int
cs5530_dma_claim(uint16_t port, uint8_t write, uint8_t val, void *priv)
{
    if (in_smm)
        return 0;

    cs5530_audio_trap(1, port, write, val, priv);
    return 1;
}

/* Fast path read: the FM status, and the Sound Blaster's FM mirrors. */
static uint8_t
cs5530_audio_fast_read(uint16_t port, void *priv)
{
    const cs5530_t *dev = (cs5530_t *) priv;

    /* SMM's cycles are not trapped: in SMM 2x0h-2x3h is the MIDI UART. */
    if (in_smm)
        return 0xff;

    /* An ISA cycle, as the OPL's own status read: the drivers time VSA's
       emulated FM timers by polling it. */
    cycles -= ISA_CYCLES(8);

    /* F3BAR+18h[10:9] are the OPL's timer flags, T1 and T2 (VSA sets bit 10
       as timer 1 starts); the status has IRQ (bit 7) with either. */
    return ((dev->io_trap_enable & 0x0400) ? 0xc0 : 0x00) | ((dev->io_trap_enable & 0x0200) ? 0xa0 : 0x00);
}

/* VSA's MPU-401 UART mode talks to a 16550 at the Sound Blaster base
   (GXM55303 VSA: the base comes from its Sound Blaster table at DS:2E0h,
   status 331h from LSR bits 0 and 6, data through RBR/THR); the CS5530
   has none, so the board has to (the P5GX-M's MIDI port works). Outside
   SMM the CS5530 claims those trapped cycles, so only VSA reaches it.
   Without it the bus floats, LSR reads FFh, and the MPU-401 always has
   input: the NT 4.0 driver's ISR then only services the MPU. VSA programs
   no baud rate, so only the data path, LSR and the scratch registers are
   modelled. The board can turn it off (cs5530_set_midi_uart_enabled()). */
static int (*cs5530_midi_uart_enabled)(void) = NULL;

void
cs5530_set_midi_uart_enabled(int (*enabled)(void))
{
    cs5530_midi_uart_enabled = enabled;
}

static int
cs5530_midi_uart_on(void)
{
    return in_smm && ((cs5530_midi_uart_enabled == NULL) || cs5530_midi_uart_enabled());
}

static uint8_t
cs5530_midi_uart_read(uint16_t port, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    uint8_t   ret = 0xff;

    if (!cs5530_midi_uart_on())
        return ret;

    switch (port & 7) {
        case 0:
            if (dev->midi_lcr & 0x80)
                ret = dev->midi_dl & 0xff;
            else if (dev->midi_rx_count) {
                ret = dev->midi_rx[dev->midi_rx_r++];
                dev->midi_rx_count--;
            } else
                ret = 0x00;
            break;
        case 1:
            ret = (dev->midi_lcr & 0x80) ? (dev->midi_dl >> 8) : dev->midi_ier;
            break;
        case 2:
            ret = 0x01;
            break;
        case 3:
            ret = dev->midi_lcr;
            break;
        case 4:
            ret = dev->midi_mcr;
            break;
        case 5:
            /* The transmitter is always empty: the byte goes out at once. */
            ret = 0x60 | (dev->midi_rx_count ? 0x01 : 0x00);
            break;
        case 6:
            ret = 0x00;
            break;
        case 7:
            ret = dev->midi_scr;
            break;

        default:
            break;
    }

    return ret;
}

static void
cs5530_midi_uart_write(uint16_t port, uint8_t val, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if (!cs5530_midi_uart_on())
        return;

    switch (port & 7) {
        case 0:
            if (dev->midi_lcr & 0x80)
                dev->midi_dl = (dev->midi_dl & 0xff00) | val;
            else
                midi_raw_out_byte(val);
            break;
        case 1:
            if (dev->midi_lcr & 0x80)
                dev->midi_dl = (dev->midi_dl & 0x00ff) | (val << 8);
            else
                dev->midi_ier = val & 0x0f;
            break;
        case 3:
            dev->midi_lcr = val;
            break;
        case 4:
            dev->midi_mcr = val & 0x1f;
            break;
        case 7:
            dev->midi_scr = val;
            break;

        default:
            break;
    }
}

static void
cs5530_midi_uart_rx(cs5530_t *dev, uint8_t val)
{
    if (dev->midi_rx_count < 256) {
        dev->midi_rx[dev->midi_rx_w++] = val;
        dev->midi_rx_count++;
    }
}

static void
cs5530_midi_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if (dev->midi_sysex)
        return;

    for (uint32_t i = 0; i < len; i++)
        cs5530_midi_uart_rx(dev, msg[i]);
}

static int
cs5530_midi_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if (abort) {
        dev->midi_sysex = 0;
        return 0;
    }
    dev->midi_sysex = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (dev->midi_rx_count >= 256)
            return (int) (len - i);
        cs5530_midi_uart_rx(dev, buffer[i]);
    }
    dev->midi_sysex = 0;
    return 0;
}

static int
cs5530_midi_input_remain(void *priv)
{
    const cs5530_t *dev = (cs5530_t *) priv;

    return 256 - dev->midi_rx_count;
}

static void
cs5530_audio_traps_recalc(cs5530_t *dev)
{
    static const uint16_t sb_base[4] = { 0x220, 0x240, 0x260, 0x280 };
    const uint16_t        en         = dev->io_trap_enable;
    const uint16_t        sb         = sb_base[en & 3];
    static uint16_t       fast_sb    = 0;
    static int            fast_on    = 0;

    for (int i = 0; i < AT_TRAPS; i++) {
        if (dev->audio_trap[i] == NULL)
            dev->audio_trap[i] = io_trap_add(cs5530_audio_trap, (void *) (intptr_t) i);
    }

    if (dev->midi_base != sb) {
        if (dev->midi_base)
            io_removehandler(dev->midi_base, 8, cs5530_midi_uart_read, NULL, NULL,
                             cs5530_midi_uart_write, NULL, NULL, dev);
        dev->midi_base = sb;
        io_sethandler(dev->midi_base, 8, cs5530_midi_uart_read, NULL, NULL,
                      cs5530_midi_uart_write, NULL, NULL, dev);
    }

    io_trap_remap(dev->audio_trap[AT_SB], !!(en & 0x0004), sb, 0x10);
    io_trap_remap(dev->audio_trap[AT_FM], !!(en & 0x0008), 0x388, 4);
    io_trap_remap(dev->audio_trap[AT_MPU_LOW], !!(en & 0x0020), 0x300, 2);
    io_trap_remap(dev->audio_trap[AT_MPU_HIGH], !!(en & 0x0040), 0x330, 2);
    /* The DMA traps take the cycle from the DMA controller: VSA does it again
       from SMM, so the controller has to see it only once. */
    for (uint16_t port = 0x000; port < 0x010; port++)
        io_set_claim(port, (en & 0x0080) ? cs5530_dma_claim : NULL, (void *) (intptr_t) AT_DMA_LOW);
    for (uint16_t port = 0x0c0; port < 0x0e0; port++)
        io_set_claim(port, (en & 0x0100) ? cs5530_dma_claim : NULL, (void *) (intptr_t) AT_DMA_HIGH);

    if (fast_on) {
        io_removehandler(0x388, 4, cs5530_audio_fast_read, NULL, NULL, NULL, NULL, NULL, dev);
        io_removehandler(fast_sb, 4, cs5530_audio_fast_read, NULL, NULL, NULL, NULL, NULL, dev);
        io_removehandler(fast_sb + 8, 2, cs5530_audio_fast_read, NULL, NULL, NULL, NULL, NULL, dev);
        fast_on = 0;
    }
    if (en & 0x0010) {
        fast_sb = sb;
        io_sethandler(0x388, 4, cs5530_audio_fast_read, NULL, NULL, NULL, NULL, NULL, dev);
        io_sethandler(fast_sb, 4, cs5530_audio_fast_read, NULL, NULL, NULL, NULL, NULL, dev);
        io_sethandler(fast_sb + 8, 2, cs5530_audio_fast_read, NULL, NULL, NULL, NULL, NULL, dev);
        fast_on = 1;
    }
}

/* clear: the bytes of the dword the read covers, for the registers a read
   clears (0 for a write's read-modify). */
static uint32_t
cs5530_f3_readl_internal(cs5530_t *dev, uint32_t addr, int clear)
{
    uint32_t ret = 0;

    /* The mapping hands over the address on the bus, not F3BAR's offset. */
    addr &= 0x7f;

    switch (addr & 0x7c) {
        case 0x00:
            ret = dev->codec_gpio_status;
            break;
        case 0x04:
            ret = dev->codec_gpio_control;
            break;
        case 0x08:
            ret = dev->codec_status;
            if (clear)
                dev->codec_status &= ~(1 << 17); /* The tag: not new any more */
            break;
        case 0x0c:
            ret = dev->codec_command;
            break;
        case 0x10: {
            /* The bus master bits are their SMI Status registers' (21h, 29h...),
               until those are read: VSA reads 21h before it clears this one
               and checks the mirror after, so an EOP in between must still
               show. The I/O trap and codec bits are latched here. */
            uint16_t smi = dev->audio_smi;

            for (int bm = 0; bm < 6; bm++) {
                if (dev->abm_smi[bm])
                    smi |= AUD_BM(bm);
            }
            ret = smi | ((uint32_t) smi << 16);
            if (clear & 0x3)
                dev->audio_smi = 0;
            break;
        }
        case 0x14:
            ret = dev->io_trap_status;
            if (clear)
                dev->io_trap_status &= ~(IOT_SB_FAST | IOT_DMA | IOT_MPU | IOT_SB);
            break;
        case 0x18:
            ret = dev->io_trap_enable | ((uint32_t) dev->irq_enable << 16);
            break;
        case 0x1c:
            ret = dev->irq_control;
            break;
        case 0x20 ... 0x4c: {
            const int bm = (addr - 0x20) >> 3;

            if (addr & 4)
                ret = dev->abm_prd[bm];
            else {
                ret = dev->abm_cmd[bm] | (dev->abm_smi[bm] << 8);
                /* The SMI status is byte 21h (+8 per master). */
                if (clear & 0x2)
                    dev->abm_smi[bm] = 0;
            }
            break;
        }

        default:
            break;
    }

    return ret;
}

static uint8_t
cs5530_f3_read(uint32_t addr, void *priv)
{
    return cs5530_f3_readl_internal((cs5530_t *) priv, addr, 1 << (addr & 3)) >> ((addr & 3) << 3);
}

static uint16_t
cs5530_f3_readw(uint32_t addr, void *priv)
{
    return cs5530_f3_readl_internal((cs5530_t *) priv, addr, 3 << (addr & 2)) >> ((addr & 2) << 3);
}

static uint32_t
cs5530_f3_readl(uint32_t addr, void *priv)
{
    return cs5530_f3_readl_internal((cs5530_t *) priv, addr, 0xf);
}

static void
cs5530_f3_write_internal(cs5530_t *dev, uint32_t addr, uint32_t val, uint32_t mask)
{
    uint32_t old;

    addr &= 0x7f;
    old = cs5530_f3_readl_internal(dev, addr, 0);

    val = (old & ~mask) | (val & mask);

    switch (addr & 0x7c) {
        case 0x00:
            dev->codec_gpio_status = (dev->codec_gpio_status & 0x001fffff) | (val & 0xc0000000);
            break;
        case 0x04:
            dev->codec_gpio_control = val & 0x000fffff;
            break;
        case 0x08:
            dev->codec_status = (dev->codec_status & 0xff03ffff) | (val & 0x00f80000);
            break;
        case 0x0c:
            /* A command to the codec: a write, or, with bit 7 of the
               address set, a read whose status comes back at 08h. */
            dev->codec_command = val & 0xffc1ffff;
            if ((mask & 0xff000000) && dev->codec && !(val & 0x00c00000)) {
                const uint8_t reg = (val >> 24) & 0x7f;

                if (val & 0x80000000)
                    dev->codec_status = (dev->codec_status & 0x00f80000) | ((uint32_t) reg << 24) | (3 << 16) |
                                        ac97_codec_readw(dev->codec, reg);
                else
                    ac97_codec_writew(dev->codec, reg, val & 0xffff);
            }
            dev->codec_command &= ~(1 << 16);
            break;
        case 0x18:
            if (mask & 0x0000ffff) {
                dev->io_trap_enable = val & 0x0fff;
                cs5530_audio_traps_recalc(dev);
            }
            if (mask & 0xffff0000)
                dev->irq_enable = (val >> 16) & 0xdefe;
            cs5530_internal_irq_update(dev);
            break;
        case 0x1c:
            if (mask & 0x0000ffff)
                dev->irq_control = (dev->irq_control & ~dev->irq_mask) | (val & dev->irq_mask & 0xdefe);
            /* The mask is write only. */
            if (mask & 0xffff0000)
                dev->irq_mask = (val >> 16) & 0xdefe;
            cs5530_internal_irq_update(dev);
            break;
        case 0x20 ... 0x4c: {
            const int bm = (addr - 0x20) >> 3;

            if (addr & 4)
                dev->abm_prd[bm] = val & 0xfffffffc;
            else
                cs5530_abm_command(dev, bm, val & 0xff);
            break;
        }

        default:
            break;
    }
}

static void
cs5530_f3_write(uint32_t addr, uint8_t val, void *priv)
{
    const int shift = (addr & 3) << 3;

    cs5530_f3_write_internal((cs5530_t *) priv, addr, val << shift, 0xff << shift);
}

static void
cs5530_f3_writew(uint32_t addr, uint16_t val, void *priv)
{
    const int shift = (addr & 2) << 3;

    cs5530_f3_write_internal((cs5530_t *) priv, addr, val << shift, 0xffff << shift);
}

static void
cs5530_f3_writel(uint32_t addr, uint32_t val, void *priv)
{
    cs5530_f3_write_internal((cs5530_t *) priv, addr, val, 0xffffffff);
}

/* ----------------------------------------------------------------------
   F4: the display interface (3.8, Table 4-22), dword accesses.
   ---------------------------------------------------------------------- */
static const uint16_t cs5530_pll_n[400] = {
    0x3ff, 0x5ff, 0x2ff, 0x57f, 0x2bf, 0x55f, 0x2af, 0x557, 0x2ab, 0x555,
    0x2aa, 0x155, 0x0aa, 0x455, 0x22a, 0x115, 0x08a, 0x445, 0x222, 0x111,
    0x088, 0x444, 0x622, 0x311, 0x588, 0x6c4, 0x362, 0x1b1, 0x0d8, 0x46c,
    0x636, 0x31b, 0x58d, 0x2c6, 0x163, 0x0b1, 0x058, 0x42c, 0x616, 0x30b,
    0x585, 0x2c2, 0x161, 0x0b0, 0x458, 0x62c, 0x316, 0x18b, 0x0c5, 0x062,
    0x431, 0x218, 0x10c, 0x486, 0x643, 0x721, 0x790, 0x3c8, 0x1e4, 0x4f2,
    0x679, 0x73c, 0x39e, 0x1cf, 0x0e7, 0x073, 0x039, 0x01c, 0x40e, 0x607,
    0x703, 0x781, 0x7c0, 0x3e0, 0x1f0, 0x4f8, 0x67c, 0x33e, 0x19f, 0x0cf,
    0x067, 0x033, 0x019, 0x00c, 0x406, 0x603, 0x701, 0x780, 0x3c0, 0x1e0,
    0x4f0, 0x678, 0x33c, 0x19e, 0x4cf, 0x267, 0x533, 0x299, 0x54c, 0x6a6,
    0x353, 0x5a9, 0x2d4, 0x16a, 0x4b5, 0x25a, 0x12d, 0x096, 0x44b, 0x225,
    0x512, 0x689, 0x744, 0x3a2, 0x1d1, 0x0e8, 0x474, 0x63a, 0x31d, 0x58e,
    0x6c7, 0x763, 0x7b1, 0x7d8, 0x3ec, 0x1f6, 0x4fb, 0x27d, 0x53e, 0x69f,
    0x74f, 0x7a7, 0x7d3, 0x7e9, 0x7f4, 0x3fa, 0x1fd, 0x0fe, 0x47f, 0x23f,
    0x51f, 0x28f, 0x547, 0x2a3, 0x551, 0x2a8, 0x154, 0x4aa, 0x655, 0x72a,
    0x395, 0x5ca, 0x6e5, 0x772, 0x3b9, 0x5dc, 0x6ee, 0x377, 0x5bb, 0x2dd,
    0x56e, 0x6b7, 0x75b, 0x7ad, 0x7d6, 0x3eb, 0x5f5, 0x2fa, 0x17d, 0x0be,
    0x45f, 0x22f, 0x517, 0x28b, 0x545, 0x2a2, 0x151, 0x0a8, 0x454, 0x62a,
    0x315, 0x58a, 0x6c5, 0x762, 0x3b1, 0x5d8, 0x6ec, 0x376, 0x1bb, 0x0dd,
    0x06e, 0x437, 0x21b, 0x50d, 0x286, 0x143, 0x0a1, 0x050, 0x428, 0x614,
    0x30a, 0x185, 0x0c2, 0x461, 0x230, 0x118, 0x48c, 0x646, 0x323, 0x591,
    0x2c8, 0x164, 0x4b2, 0x659, 0x72c, 0x396, 0x1cb, 0x0e5, 0x072, 0x439,
    0x21c, 0x10e, 0x487, 0x243, 0x521, 0x290, 0x148, 0x4a4, 0x652, 0x329,
    0x594, 0x6ca, 0x365, 0x5b2, 0x6d9, 0x76c, 0x3b6, 0x1db, 0x0ed, 0x076,
    0x43b, 0x21d, 0x50e, 0x687, 0x743, 0x7a1, 0x7d0, 0x3e8, 0x1f4, 0x4fa,
    0x67d, 0x73e, 0x39f, 0x5cf, 0x2e7, 0x573, 0x2b9, 0x55c, 0x6ae, 0x357,
    0x5ab, 0x2d5, 0x56a, 0x6b5, 0x75a, 0x3ad, 0x5d6, 0x6eb, 0x775, 0x7ba,
    0x3dd, 0x5ee, 0x6f7, 0x77b, 0x7bd, 0x7de, 0x3ef, 0x5f7, 0x2fb, 0x57d,
    0x2be, 0x15f, 0x0af, 0x057, 0x02b, 0x015, 0x00a, 0x405, 0x202, 0x101,
    0x080, 0x440, 0x620, 0x310, 0x188, 0x4c4, 0x662, 0x331, 0x598, 0x6cc,
    0x366, 0x1b3, 0x0d9, 0x06c, 0x436, 0x61b, 0x70d, 0x786, 0x3c3, 0x5e1,
    0x2f0, 0x178, 0x4bc, 0x65e, 0x32f, 0x597, 0x2cb, 0x565, 0x2b2, 0x159,
    0x0ac, 0x456, 0x62b, 0x715, 0x78a, 0x3c5, 0x5e2, 0x6f1, 0x778, 0x3bc,
    0x1de, 0x4ef, 0x277, 0x53b, 0x29d, 0x54e, 0x6a7, 0x753, 0x7a9, 0x7d4,
    0x3ea, 0x1f5, 0x0fa, 0x47d, 0x23e, 0x11f, 0x08f, 0x047, 0x023, 0x011,
    0x008, 0x404, 0x602, 0x301, 0x580, 0x6c0, 0x360, 0x1b0, 0x4d8, 0x66c,
    0x336, 0x19b, 0x0cd, 0x066, 0x433, 0x219, 0x50c, 0x686, 0x343, 0x5a1,
    0x2d0, 0x168, 0x4b4, 0x65a, 0x32d, 0x596, 0x6cb, 0x765, 0x7b2, 0x3d9,
    0x5ec, 0x6f6, 0x37b, 0x5bd, 0x2de, 0x16f, 0x0b7, 0x05b, 0x02d, 0x016,
    0x40b, 0x205, 0x502, 0x681, 0x740, 0x3a0, 0x1d0, 0x4e8, 0x674, 0x33a,
};

/* DCLK = 14.31818 MHz * FD / (PD * ID): FD = 2N (+1 with bit 23), N from
   its LFSR code, PD and ID from their codes (Table 4-22, Table 4-23). */
static double
cs5530_dotclock(cs5530_t *dev)
{
    static const uint8_t pd_div[32] = { 8, 6, 18, 4, 12, 16, 24, 2, 10, 20, 14, 26, 22, 28, 30, 1,
                                        9, 7, 19, 5, 13, 17, 25, 3, 11, 21, 15, 27, 23, 29, 31, 0 };
    const uint32_t       cfg        = dev->f4_regs[0x24 >> 2];
    const uint16_t       code       = (cfg >> 12) & 0x7ff;
    int                  n          = 0;
    int                  pd;
    int                  fd;
    int                  id;

    if ((cfg & (1 << 31)) || !(cfg & (1 << 11)))
        return 0.0;
    /* The PLL bypassed: the 14 MHz reference itself. */
    if (cfg & (1 << 8))
        return 14318180.0;

    for (int i = 0; i < 400; i++) {
        if (cs5530_pll_n[i] == code) {
            n = i + 1;
            break;
        }
    }

    pd = pd_div[(cfg >> 24) & 0x1f];
    id = (cfg & 7) + 2;
    fd = (n << 1) + !!(cfg & (1 << 23));
    if (!n || !pd)
        return 0.0;

    return 14318180.0 * (double) fd / (double) (pd * id);
}

static uint32_t
cs5530_f4_readl(uint32_t addr, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    uint32_t  ret = 0;

    addr &= 0xffc;

    switch (addr) {
        case 0x00:
        case 0x08 ... 0x1c:
            ret = dev->f4_regs[addr >> 2];
            break;
        case 0x04:
            /* The comparators see a monitor's load. */
            ret = dev->f4_regs[1] & 0x07ffffff;
            break;
        case 0x20:
            ret = dev->gamma[dev->gamma_addr++];
            break;
        case 0x24:
            /* Locked as soon as it runs. */
            ret = dev->f4_regs[0x24 >> 2] | ((dev->f4_regs[0x24 >> 2] & (1 << 11)) ? (1 << 5) : 0);
            break;
        case 0x28:
            ret = dev->f4_regs[0x28 >> 2];
            break;

        default:
            break;
    }

    return ret;
}

static uint8_t
cs5530_f4_read(uint32_t addr, void *priv)
{
    return cs5530_f4_readl(addr, priv) >> ((addr & 3) << 3);
}

static uint16_t
cs5530_f4_readw(uint32_t addr, void *priv)
{
    return cs5530_f4_readl(addr, priv) >> ((addr & 2) << 3);
}

static void
cs5530_f4_writel(uint32_t addr, uint32_t val, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    addr &= 0xffc;

    cs5530_log("CS5530: [W] F4BAR+%02X = %08X\n", addr, val);

    switch (addr) {
        case 0x00:
        case 0x08 ... 0x18: {
            /* The video overlay: configuration, X and Y window, scale, color
               key and mask (Tables 3-71 to 3-74). */
            static const uint32_t vid_mask[7] = { 0x7bffffff, 0, 0x07ff07ff, 0x07ff07ff, 0x3fff3fff, 0x00ffffff, 0x00ffffff };

            dev->f4_regs[addr >> 2] = val & vid_mask[addr >> 2];
            gxm_video_set_overlay(gxm_video, addr >> 2, dev->f4_regs[addr >> 2]);
            break;
        }
        case 0x04:
            dev->f4_regs[1] = val & 0x07ffffef;
            gxm_video_set_display(gxm_video, dev->f4_regs[1], NULL);
            break;
        case 0x1c:
            dev->f4_regs[0x1c >> 2] = val & 0xff;
            dev->gamma_addr         = val & 0xff;
            break;
        case 0x20:
            dev->gamma[dev->gamma_addr++] = val & 0x00ffffff;
            gxm_video_set_display(gxm_video, dev->f4_regs[1], dev->gamma);
            break;
        case 0x24:
            dev->f4_regs[0x24 >> 2] = val & 0xdfffff07;
            gxm_video_set_dotclock(gxm_video, cs5530_dotclock(dev));
            break;
        case 0x28:
            dev->f4_regs[0x28 >> 2] = val & 0x000000e7;
            break;

        default:
            break;
    }
}

static void
cs5530_f4_write(uint32_t addr, uint8_t val, void *priv)
{
    /* All four bytes are to be written; a narrower write merges. */
    const int shift = (addr & 3) << 3;
    uint32_t  old   = cs5530_f4_readl(addr & ~3, priv);

    if ((addr & 0xffc) == 0x20)
        return;
    cs5530_f4_writel(addr, (old & ~(0xff << shift)) | (val << shift), priv);
}

static void
cs5530_f4_writew(uint32_t addr, uint16_t val, void *priv)
{
    const int shift = (addr & 2) << 3;
    uint32_t  old   = cs5530_f4_readl(addr & ~3, priv);

    if ((addr & 0xffc) == 0x20)
        return;
    cs5530_f4_writel(addr, (old & ~(0xffff << shift)) | (val << shift), priv);
}

/* ----------------------------------------------------------------------
   F2: IDE (3.6, Table 4-18). The channels are at the legacy addresses,
   on while their positive decode is (F0 5Bh[4:3]).
   ---------------------------------------------------------------------- */
static void
cs5530_ide_handlers(cs5530_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();

    ide_set_base(0, 0x1f0);
    ide_set_side(0, 0x3f6);
    ide_set_base(1, 0x170);
    ide_set_side(1, 0x376);

    if (dev->pci_conf[0][0x5b] & 0x08)
        ide_pri_enable();
    if (dev->pci_conf[0][0x5b] & 0x10)
        ide_sec_enable();
}

static uint8_t
cs5530_ide_timing_read(uint16_t port, void *priv)
{
    const cs5530_t *dev = (cs5530_t *) priv;
    const int       off = port - dev->f2_base - 0x20;

    return dev->ide_timing[off >> 2] >> ((off & 3) << 3);
}

static uint32_t
cs5530_ide_timing_readl(uint16_t port, void *priv)
{
    const cs5530_t *dev = (cs5530_t *) priv;

    return dev->ide_timing[((port - dev->f2_base - 0x20) >> 2) & 7];
}

static void
cs5530_ide_timing_write(uint16_t port, uint8_t val, void *priv)
{
    cs5530_t *dev   = (cs5530_t *) priv;
    const int off   = port - dev->f2_base - 0x20;
    const int shift = (off & 3) << 3;

    dev->ide_timing[off >> 2] = (dev->ide_timing[off >> 2] & ~(0xff << shift)) | (val << shift);
}

static void
cs5530_ide_timing_writel(uint16_t port, uint32_t val, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    dev->ide_timing[((port - dev->f2_base - 0x20) >> 2) & 7] = val;
}

static void
cs5530_ide_bm_handlers(cs5530_t *dev)
{
    const int enable = dev->pci_conf[2][0x04] & 0x01;

    if (dev->f2_base)
        io_removehandler(dev->f2_base + 0x20, 0x20,
                         cs5530_ide_timing_read, NULL, cs5530_ide_timing_readl,
                         cs5530_ide_timing_write, NULL, cs5530_ide_timing_writel, dev);

    dev->f2_base = (dev->pci_conf[2][0x20] & 0x80) | (dev->pci_conf[2][0x21] << 8);

    sff_bus_master_handler(dev->bm[0], enable, dev->f2_base);
    sff_bus_master_handler(dev->bm[1], enable, dev->f2_base + 8);

    if (enable && dev->f2_base)
        io_sethandler(dev->f2_base + 0x20, 0x20,
                      cs5530_ide_timing_read, NULL, cs5530_ide_timing_readl,
                      cs5530_ide_timing_write, NULL, cs5530_ide_timing_writel, dev);
}

/* ----------------------------------------------------------------------
   PCI configuration.
   ---------------------------------------------------------------------- */
static void
cs5530_irq_routing(cs5530_t *dev)
{
    static const int irq_map[16] = { PCI_IRQ_DISABLED, 1, PCI_IRQ_DISABLED, 3, 4, 5, 6, 7,
                                     PCI_IRQ_DISABLED, 9, 10, 11, 12, PCI_IRQ_DISABLED, 14, 15 };

    pci_set_irq_routing(PCI_INTA, irq_map[dev->pci_conf[0][0x5c] & 0x0f]);
    pci_set_irq_routing(PCI_INTB, irq_map[dev->pci_conf[0][0x5c] >> 4]);
    pci_set_irq_routing(PCI_INTC, irq_map[dev->pci_conf[0][0x5d] & 0x0f]);
    pci_set_irq_routing(PCI_INTD, irq_map[dev->pci_conf[0][0x5d] >> 4]);
}

static void
cs5530_bar_remap(cs5530_t *dev, int func)
{
    const uint8_t *conf = dev->pci_conf[func];
    const uint32_t bar  = conf[0x10] | (conf[0x11] << 8) | (conf[0x12] << 16) | (conf[0x13] << 24);
    const int      on   = (conf[0x04] & 0x02) && bar;

    switch (func) {
        case 1:
            /* F1BAR decodes with the command register's memory space bit
               clear too ("always memory mapped", Table 4-15): when Windows
               stops the device (command and BARs zeroed), VSA puts back F1BAR
               alone (VSA 0000:0325h) and then polls the SMI status there. */
            if (bar)
                mem_mapping_set_addr(&dev->f1_mapping, bar & 0xffffff00, 0x100);
            else
                mem_mapping_disable(&dev->f1_mapping);
            break;
        case 3:
            if (on)
                mem_mapping_set_addr(&dev->f3_mapping, bar & 0xffffff80, 0x80);
            else
                mem_mapping_disable(&dev->f3_mapping);
            break;
        case 4:
            if (on)
                mem_mapping_set_addr(&dev->f4_mapping, bar & 0xfffff000, 0x1000);
            else
                mem_mapping_disable(&dev->f4_mapping);
            break;

        default:
            break;
    }
}

static void
cs5530_f0_write(cs5530_t *dev, int addr, uint8_t val)
{
    uint8_t *conf = dev->pci_conf[0];

    switch (addr) {
        case 0x04:
            conf[addr] = (val & 0x5f) | 0x04;
            break;
        case 0x05:
            conf[addr] = val & 0x01;
            break;
        case 0x07:
            conf[addr] &= ~(val & 0xf9);
            break;
        case 0x0c:
            conf[addr] = val;
            break;
        case 0x0d:
            conf[addr] = val & 0x0f;
            break;
        case 0x40:
            conf[addr] = (val & 0xf9) | 0x01;
            break;
        case 0x41:
        case 0x42:
            conf[addr] = val;
            cs5530_smi_update(dev);
            break;
        case 0x43:
            conf[addr] = val & 0x7f;
            break;
        case 0x44:
            /* The straps read back; the resets are levels. */
            if ((val & 0x01) && !(conf[addr] & 0x01))
                resetx86();
            conf[addr] = (conf[addr] & 0xc0) | (val & 0x3e);
            if (val & 0x08) {
                sff_bus_master_reset(dev->bm[0]);
                sff_bus_master_reset(dev->bm[1]);
            }
            break;
        case 0x50:
        case 0x51:
            conf[addr] = val;
            break;
        case 0x52:
            conf[addr] = val;
            port_92_set_features(dev->port_92, !!(val & 0x08), !!(val & 0x08));
            if (val & 0x08)
                port_92_add(dev->port_92);
            else
                port_92_remove(dev->port_92);
            break;
        case 0x53:
            conf[addr] = (val & 0x7f) | 0x02;
            cs5530_pm_traps_recalc(dev);
            break;
        case 0x5a:
            conf[addr] = val;
            break;
        case 0x5b:
            conf[addr] = val & 0xbf;
            cs5530_ide_handlers(dev);
            break;
        case 0x5c:
        case 0x5d:
            conf[addr] = val;
            cs5530_irq_routing(dev);
            break;
        case 0x70 ... 0x72:
            conf[addr] = val;
            break;
        case 0x80 ... 0x83: {
            /* The timers load their counts as they are enabled; an enable
               bit written again as 1 leaves a running or expired timer be
               (VSA rewrites 83h on every SMI). */
            const uint8_t on = (addr == 0x83) ? (val & ~conf[0x83]) : 0x00;

            conf[addr] = val;
            if (addr == 0x83) {
                if (on & 0x01)
                    dev->gpt_count[0] = (conf[0x8b] & 0x10) ? (conf[0x88] << 8) : conf[0x88];
                if (on & 0x02)
                    dev->gpt_count[1] = (conf[0x8b] & 0x20) ? (conf[0x8a] << 8) : conf[0x8a];
                if ((on & 0x08) || !(val & 0x08))
                    cs5530_vga_timer_load(dev);
            }
            cs5530_pm_traps_recalc(dev);
            cs5530_ms_timer_recalc(dev);
            break;
        }
        case 0x88:
            conf[addr]        = val;
            dev->gpt_count[0] = (conf[0x8b] & 0x10) ? (val << 8) : val;
            break;
        case 0x8a:
            conf[addr]        = val;
            dev->gpt_count[1] = (conf[0x8b] & 0x20) ? (val << 8) : val;
            break;
        case 0x89:
        case 0x8b:
            conf[addr] = (addr == 0x8b) ? (val & 0xfc) : val;
            cs5530_pm_traps_recalc(dev);
            break;
        case 0x8c:
        case 0x8d:
            conf[addr] = val;
            break;
        case 0x8e:
            conf[addr] = val;
            cs5530_vga_timer_load(dev);
            break;
        case 0x90:
        case 0x92:
            conf[addr] = val;
            break;
        case 0x91:
            /* The outputs read back as written, the inputs float high. */
            conf[addr] = (val & conf[0x90]) | ~conf[0x90];
            break;
        case 0x93:
            conf[addr] = val | 0x40;
            cs5530_pm_traps_recalc(dev);
            break;
        case 0x94 ... 0x97:
            conf[addr] = (addr == 0x96) ? (val & 0x07) : val;
            break;
        case 0x98 ... 0xa9:
        case 0xac:
        case 0xad:
            conf[addr] = val;
            break;
        case 0xae:
        case 0xaf:
            /* The CPU Suspend commands: a SUSP#/SUSPA# handshake, the
               processor halted till an interrupt or SMI. */
            break;
        case 0xbc:
            conf[addr] = val & 0xf1;
            break;
        case 0xc0 ... 0xce:
            conf[addr] = val;
            cs5530_pm_traps_recalc(dev);
            break;
        case 0xd0:
            /* The software SMI. */
            cs5530_smi(dev, TOP_SOFTWARE);
            break;
        case 0xec:
            conf[addr] = val;
            break;

        default:
            break;
    }
}

static uint8_t
cs5530_f0_read(cs5530_t *dev, int addr)
{
    uint8_t *conf = dev->pci_conf[0];
    uint8_t  ret  = conf[addr];

    switch (addr) {
        case 0x84 ... 0x87:
            ret = dev->pm_smi[addr - 0x84];
            break;
        case 0xf4 ... 0xf7:
            ret = dev->pm_smi[addr - 0xf4];
            /* Read to clear; the lid position stays. */
            dev->pm_smi[addr - 0xf4] &= (addr == 0xf7) ? 0x10 : 0x00;
            break;
        case 0xb4 ... 0xbb:
            /* The shadow registers. */
            ret = 0x00;
            break;
        case 0xd0:
        case 0xae:
        case 0xaf:
            ret = 0x00;
            break;

        default:
            break;
    }

    return ret;
}

/* The configuration traps of the functions: an SMI after the access, so
   VSA can make up the value of a read (F0 41h-42h, F1BAR+04h[5]). */
static int
cs5530_cfg_trapped(cs5530_t *dev, int func)
{
    static const uint8_t reg[5] = { 0, 0x41, 0x41, 0x42, 0x42 };
    static const uint8_t bit[5] = { 0, 0x08, 0x40, 0x01, 0x02 };

    return func && (func <= 4) && (dev->pci_conf[0][reg[func]] & bit[func]);
}

static void
cs5530_cfg_trap(cs5530_t *dev, int func)
{
    if (cs5530_cfg_trapped(dev, func))
        cs5530_gen_smi(dev, GEN_PCI_TRAP);
}

static void
cs5530_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    uint8_t  *conf;

    if (func > 4)
        return;

    cs5530_log("CS5530: [W] F%i %02X = %02X\n", func, addr, val);

    /* A trapped header is VSA's: it keeps its own, and the registers stay as
       it set them. Windows sizes the XpressGRAPHICS BAR VSA makes up as the
       8 MB of graphics memory and moves it there; moved, F4BAR would take
       the frame buffer's writes, and VSA would lose the registers it keeps
       using at the old address. */
    if (!in_smm && cs5530_cfg_trapped(dev, func)) {
        cs5530_gen_smi(dev, GEN_PCI_TRAP);
        return;
    }

    conf = dev->pci_conf[func];

    if (func == 0)
        cs5530_f0_write(dev, addr, val);
    else switch (addr) {
        case 0x04:
            switch (func) {
                case 1:
                case 4:
                    conf[addr] = val & 0x02;
                    break;
                case 2:
                    conf[addr] = val & 0x05;
                    cs5530_ide_bm_handlers(dev);
                    break;
                case 3:
                    conf[addr] = val & 0x06;
                    break;

                default:
                    break;
            }
            cs5530_bar_remap(dev, func);
            break;
        case 0x10 ... 0x13:
            if (func == 2)
                break;
            if (addr == 0x10)
                val &= (func == 1) ? 0x00 : ((func == 3) ? 0x80 : 0x00);
            if ((addr == 0x11) && (func == 4))
                val &= 0xf0;
            conf[addr] = val;
            cs5530_bar_remap(dev, func);
            break;
        case 0x20 ... 0x23:
            if (func != 2)
                break;
            conf[addr] = (addr == 0x20) ? ((val & 0x80) | 0x01) : val;
            cs5530_ide_bm_handlers(dev);
            break;

        default:
            break;
    }

    cs5530_cfg_trap(dev, func);
}

static uint8_t
cs5530_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    uint8_t   ret;

    if (func > 4)
        return 0xff;

    if (func == 0)
        ret = cs5530_f0_read(dev, addr);
    else
        ret = dev->pci_conf[func][addr];

    cs5530_cfg_trap(dev, func);

    return ret;
}

/* The USB controller, its own PCI device (Table 4-24), on INTA# of its
   slot. */
static void
cs5530_usb_irq(void *priv, int level)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if (level)
        pci_set_irq(dev->usb_slot, PCI_INTA, &dev->usb_irq_state);
    else
        pci_clear_irq(dev->usb_slot, PCI_INTA, &dev->usb_irq_state);
}

/* Its SMI, a level: it holds SMI# low with 42h[7], so an RSM before the
   handler clears the condition is the next SMI. The top level status (which
   VSA dispatches on) latches it as it asserts, with either bit. */
static void
cs5530_usb_smi(void *priv, int level)
{
    cs5530_t *dev = (cs5530_t *) priv;

    dev->usb_smi_level = level;
    if (level && (dev->pci_conf[0][0x42] & 0xc0))
        dev->top_smi |= TOP_USB;
    cs5530_smi_update(dev);
}

static void
cs5530_usb_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    if (func)
        return;

    switch (addr) {
        case 0x04:
            dev->usb_conf[addr] = val & 0x57;
            ohci_update_mem_mapping(dev->usb, dev->usb_conf[0x11], dev->usb_conf[0x12], dev->usb_conf[0x13],
                                    dev->usb_conf[0x04] & 0x02);
            ohci_set_bus_master(dev->usb, dev->usb_conf[0x04] & 0x04);
            break;
        case 0x05:
            dev->usb_conf[addr] = val & 0x01;
            break;
        case 0x07:
            dev->usb_conf[addr] &= ~(val & 0xf9);
            break;
        case 0x0c:
        case 0x0d:
        case 0x3c:
        case 0x40 ... 0x44:
            dev->usb_conf[addr] = val;
            break;
        case 0x11 ... 0x13:
            dev->usb_conf[addr] = (addr == 0x11) ? (val & 0xf0) : val;
            ohci_update_mem_mapping(dev->usb, dev->usb_conf[0x11], dev->usb_conf[0x12], dev->usb_conf[0x13],
                                    dev->usb_conf[0x04] & 0x02);
            break;

        default:
            break;
    }
}

static uint8_t
cs5530_usb_read(int func, int addr, UNUSED(int len), void *priv)
{
    const cs5530_t *dev = (cs5530_t *) priv;

    if (func)
        return 0xff;

    return dev->usb_conf[addr];
}

static void
cs5530_reset(void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;
    uint8_t  *conf;

    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    for (int f = 0; f < 5; f++) {
        static const uint8_t  class[5][3] = { { 0x00, 0x01, 0x06 }, { 0x00, 0x80, 0x06 }, { 0x80, 0x01, 0x01 },
                                              { 0x00, 0x01, 0x04 }, { 0x00, 0x00, 0x03 } };

        conf       = dev->pci_conf[f];
        conf[0x00] = 0x78;
        conf[0x01] = 0x10;
        conf[0x02] = f;
        conf[0x03] = 0x01;
        conf[0x06] = 0x80;
        conf[0x07] = 0x02;
        conf[0x09] = class[f][0];
        conf[0x0a] = class[f][1];
        conf[0x0b] = class[f][2];
    }

    conf       = dev->pci_conf[0];
    conf[0x04] = 0x0f;
    conf[0x08] = 0x00;
    conf[0x0e] = 0x80; /* Multi-function */
    conf[0x40] = 0x89;
    conf[0x41] = 0x10;
    conf[0x42] = 0x0f;
    conf[0x43] = 0x03;
    /* ISA master mode; AD28 is the IDSEL. */
    conf[0x44] = 0x80;
    conf[0x50] = 0x7b;
    conf[0x51] = 0x40;
    conf[0x52] = 0xf8;
    conf[0x53] = 0x02;
    conf[0x5a] = 0x03;
    conf[0x5b] = 0x20;
    conf[0x91] = 0xff;
    conf[0x93] = 0x40;

    dev->pci_conf[2][0x20] = 0x01;

    memset(dev->pm_smi, 0x00, sizeof(dev->pm_smi));
    dev->pm_smi[0] = 0x00;
    dev->top_smi   = 0x0000;
    dev->gen_smi   = 0x0000;
    dev->smi_level = 0;
    dev->a20m      = 0;

    dev->codec_gpio_status  = 0;
    dev->codec_gpio_control = 0;
    dev->codec_status       = 0;
    dev->codec_command      = 0;
    dev->audio_smi          = 0;
    dev->io_trap_status     = 0;
    dev->io_trap_enable     = 0;
    dev->irq_enable         = 0;
    dev->irq_control        = 0;
    dev->irq_mask           = 0;
    cs5530_internal_irq_update(dev);
    memset(dev->abm_cmd, 0x00, sizeof(dev->abm_cmd));
    memset(dev->abm_smi, 0x00, sizeof(dev->abm_smi));
    memset(dev->abm_prd, 0x00, sizeof(dev->abm_prd));
    memset(dev->abm, 0x00, sizeof(dev->abm));
    dev->abm_out_l = dev->abm_out_r = 0;
    timer_disable(&dev->abm_timer);
    cs5530_audio_traps_recalc(dev);

    memset(dev->f4_regs, 0x00, sizeof(dev->f4_regs));
    dev->f4_regs[0x28 >> 2] = 0x00000100;
    gxm_video_set_display(gxm_video, 0, NULL);
    gxm_video_set_dotclock(gxm_video, 0.0);
    for (int i = 0; i < 7; i++)
        gxm_video_set_overlay(gxm_video, i, 0);

    for (int i = 0; i < 8; i++)
        dev->ide_timing[i] = (i & 1) ? 0x00017771 : 0x0000e132;

    memset(dev->usb_conf, 0x00, sizeof(dev->usb_conf));
    dev->usb_conf[0x00] = 0x11; /* Compaq OpenHCI */
    dev->usb_conf[0x01] = 0x0e;
    dev->usb_conf[0x02] = 0xf8;
    dev->usb_conf[0x03] = 0xa0;
    dev->usb_conf[0x06] = 0x80;
    dev->usb_conf[0x07] = 0x02;
    dev->usb_conf[0x09] = 0x10;
    dev->usb_conf[0x0a] = 0x03;
    dev->usb_conf[0x0b] = 0x0c;
    dev->usb_conf[0x3d] = 0x01;
    ohci_update_mem_mapping(dev->usb, 0, 0, 0, 0);

    sff_bus_master_reset(dev->bm[0]);
    sff_bus_master_reset(dev->bm[1]);
    cs5530_ide_bm_handlers(dev);
    cs5530_ide_handlers(dev);

    for (int f = 1; f <= 4; f++)
        cs5530_bar_remap(dev, f);

    cs5530_irq_routing(dev);
    cs5530_pm_traps_recalc(dev);
    cs5530_ms_timer_recalc(dev);
    timer_disable(&dev->vga_timer);

    port_92_set_features(dev->port_92, 1, 1);
    port_92_add(dev->port_92);
}

static void
cs5530_close(void *priv)
{
    cs5530_t *dev = (cs5530_t *) priv;

    mem_a20_trap      = NULL;
    mem_a20_trap_priv = NULL;
    gxm.smi_pin       = NULL;
    gxm.serial_packet = NULL;
    cs5530_midi_uart_enabled = NULL;
    cs5530            = NULL;

    for (int i = 0; i < PM_DEVICES; i++)
        for (int j = 0; j < 6; j++)
            if (dev->pm_trap[i][j] != NULL)
                io_trap_remove(dev->pm_trap[i][j]);
    for (int i = 0; i < AT_TRAPS; i++)
        if (dev->audio_trap[i] != NULL)
            io_trap_remove(dev->audio_trap[i]);
    if (dev->midi_base)
        io_removehandler(dev->midi_base, 8, cs5530_midi_uart_read, NULL, NULL,
                         cs5530_midi_uart_write, NULL, NULL, dev);
    for (uint16_t port = 0x000; port < 0x010; port++)
        io_set_claim(port, NULL, NULL);
    for (uint16_t port = 0x0c0; port < 0x0e0; port++)
        io_set_claim(port, NULL, NULL);

    free(dev);
}

static void *
cs5530_init(UNUSED(const device_t *info))
{
    cs5530_t *dev = (cs5530_t *) calloc(1, sizeof(cs5530_t));

    cs5530 = dev;

    pci_add_card(PCI_ADD_SOUTHBRIDGE, cs5530_pci_read, cs5530_pci_write, dev, &dev->pci_slot);

    dev->usb = device_add(&usb_device);
    pci_add_card(PCI_ADD_SOUTHBRIDGE_USB, cs5530_usb_read, cs5530_usb_write, dev, &dev->usb_slot);
    ohci_set_irq_handler(dev->usb, cs5530_usb_irq, dev);
    ohci_set_smi_handler(dev->usb, cs5530_usb_smi, dev);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_irq_mode(dev->bm[0], IRQ_MODE_LEGACY);
    sff_set_irq_mode(dev->bm[1], IRQ_MODE_LEGACY);

    dev->port_92 = device_add(&port_92_pci_one_device);

    mem_mapping_add(&dev->f1_mapping, 0, 0,
                    cs5530_f1_read, cs5530_f1_readw, cs5530_f1_readl,
                    cs5530_f1_write, cs5530_f1_writew, cs5530_f1_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->f3_mapping, 0, 0,
                    cs5530_f3_read, cs5530_f3_readw, cs5530_f3_readl,
                    cs5530_f3_write, cs5530_f3_writew, cs5530_f3_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->f4_mapping, 0, 0,
                    cs5530_f4_read, cs5530_f4_readw, cs5530_f4_readl,
                    cs5530_f4_write, cs5530_f4_writew, cs5530_f4_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->f1_mapping);
    mem_mapping_disable(&dev->f3_mapping);
    mem_mapping_disable(&dev->f4_mapping);

    /* The ACPI timer at 121Ch (silicon revision 1.3 on). */
    io_sethandler(0x121c, 4, cs5530_acpi_port_read, NULL, cs5530_acpi_port_readl, NULL, NULL, NULL, dev);

    /* The codec on the AC-link. */
    ac97_codec       = &dev->codec;
    ac97_codec_count = 1;
    ac97_codec_id    = 0;
    device_add(ac97_codec_get(AC97_CODEC_AD1881));

    timer_add(&dev->ms_timer, cs5530_ms_timer, dev, 0);
    timer_add(&dev->vga_timer, cs5530_vga_timer, dev, 0);
    timer_add(&dev->acpi_timer, cs5530_acpi_timer, dev, 0);
    timer_add(&dev->abm_timer, cs5530_abm_timer, dev, 0);
    sound_add_handler(cs5530_abm_get_buffer, dev);
    midi_in_handler(1, cs5530_midi_input_msg, cs5530_midi_input_sysex, cs5530_midi_input_remain, dev);
    timer_set_delay_u64(&dev->acpi_timer, (uint64_t) ((double) TIMER_USEC * 8388608.0 * 1000000.0 / ACPI_TIMER_FREQ));

    /* ELCR, 4D0h-4D1h. */
    pic_elcr_io_handler(1);

    mem_a20_trap      = cs5530_a20_trap;
    mem_a20_trap_priv = dev;
    gxm.smi_pin       = cs5530_smi_pin;
    gxm.serial_packet = cs5530_serial_packet;

    cs5530_reset(dev);

    return dev;
}

const device_t cs5530_device = {
    .name          = "Cyrix Cx5530 I/O Companion",
    .internal_name = "cs5530",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = cs5530_init,
    .close         = cs5530_close,
    .reset         = cs5530_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* XpressAUDIO is the Cx5530's audio, driven by VSA in SMM: the Cx5530 device
   models it in full, so this only names it as the machine's sound device. */
const device_t cs5530_xpressaudio_device = {
    .name          = "Cyrix XpressAUDIO",
    .internal_name = "xpressaudio",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
