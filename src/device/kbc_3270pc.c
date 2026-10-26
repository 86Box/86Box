/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM 3270 PC keyboard adapter emulation.
 *
 *          The 5271's keyboard controller is a full-length ISA card carrying
 *          a custom MCU, an 8254 and the option ROM for every other 3270 card
 *          in the machine -- which is why the display adapter cannot work
 *          without it.  It decodes eight ports:
 *
 *            0x1B0  command register, a bitmap selecting what 0x1B2 returns
 *            0x1B1  data out, to the keyboard or to the card
 *            0x1B2  data in, multiplexed by the last 0x1B0 write
 *            0x1B3  a second status port; purpose largely unknown
 *            0x1B4-0x1B7  the on-card 8254
 *
 *          Only the card's own POST is emulated, which is what the adapter
 *          ROM tests before it will report a healthy machine.  No keyboard
 *          protocol is implemented: the 122-key 3270 keyboard does not exist
 *          in 86Box, so the ROM correctly reports POST code 0302, exactly as
 *          a real 5271 does when an ordinary PC/XT keyboard is attached.
 *
 *          Documentation is limited to John Elliott's reverse engineering at
 *          <http://www.seasip.info/VintagePC/5271.html> plus disassembly of
 *          the adapter ROM (roms/video/ibm3270pc/6323581.bin, the 6K image
 *          mapped at CA000).  ROM addresses in the comments below refer to
 *          that image.  Registers whose behaviour is inferred rather than
 *          known are marked as such.
 *
 * Authors: Anatoliy Sova, <anatoliysova@gmail.com>
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/device.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/keyboard.h>
#include <86box/plat_unused.h>

/* What a read of the card's receive queue answers with when no 3270 keyboard
   is attached.  Deliberately not 0xFA (the keyboard ACK), so the ROM reports
   0302 as a real 5271 does -- and 0x01 specifically, because the alternate
   init path at CA106 does "cmp al,1 / je CA16E" and CA16E is a bare RET. */
#define KBC3270_RX_IDLE 0x01

typedef struct kbc3270_t {
    uint8_t cmd;    /* last write to 0x1B0; selects the 0x1B2 read source */
    uint8_t data;   /* last write to 0x1B1; read back under command 0x21  */
    uint8_t status; /* the idle 0x1B2 value; only bit 0 is ever set       */

    pitf_t *pit; /* the on-card 8254 at 0x1B4-0x1B7 */
} kbc3270_t;

/*
 * Status bits, and why every one of them reads back clear:
 *
 *   0  toggled by each command with bit 7 set; CA5C7 wants it set after the
 *      first such command and CA5D7 wants it clear after the second.
 *   5  keyboard transmit complete.  MUST always read 0.  It is the only way
 *      into the unbounded handshake loop at CA0CE (via "je" at CA0B9), which
 *      spins until bits 6 and 7 both set.  With bit 5 clear the bounded poll
 *      at CA0AF simply times out and the routine returns at CA0C1.
 *   6  receive byte available.  Clear, so the drain loops at CA596 and CA62B
 *      exit immediately.
 *   7  "this card raised IRQ2".  MUST always read 0: the ROM installs a
 *      shared IRQ2 poll chain at CA1B5 that does a bare read of 0x1B2 and
 *      dispatches to the scancode handler if bit 7 is set.  The display
 *      adapter shares IRQ2, so a stuck bit 7 would hijack its interrupts.
 */

/* The 0303 test unmasks IRQ2 and waits for the on-card timer.  ponytail: the
   counter's OUT is wired straight to IRQ2; no cascade from counter 0. */
static void
kbc3270_timer_out(int new_out, int old_out, UNUSED(void *priv))
{
    if (new_out && !old_out)
        picint(1 << 2);
}

static void
kbc3270_out(uint16_t addr, uint8_t val, void *priv)
{
    kbc3270_t *dev = (kbc3270_t *) priv;

    switch (addr) {
        case 0x01b0:
            dev->cmd = val;
            /* Observed, not documented: two commands with bit 7 set leave
               bit 0 set then clear again (CA5C7, CA5D7). */
            if (val & 0x80)
                dev->status ^= 0x01;
            /* CA5EA injects a byte into the receive path and expects the
               card to interrupt. */
            if (val == 0x81)
                picint(1 << 2);
            break;

        case 0x01b1:
            dev->data = val;
            break;

        default:
            /* 0x1B2 is read-only, and the write to 0x1B3 at CA6EC just puts
               back the byte the ROM has only just read. */
            break;
    }
}

static uint8_t
kbc3270_in(uint16_t addr, void *priv)
{
    const kbc3270_t *dev = (kbc3270_t *) priv;
    uint8_t          ret = 0xff;

    switch (addr) {
        case 0x01b2:
            switch (dev->cmd) {
                case 0x01:
                    ret = 0x10; /* CA5FE tests bit 4 */
                    break;
                case 0x20:
                    ret = KBC3270_RX_IDLE;
                    break;
                case 0x21:
                    ret = dev->data; /* CA60A wants the 0x55 it just wrote */
                    break;
                default:
                    ret = dev->status;
                    break;
            }
            break;

        case 0x01b3:
            /* Inferred stub, not a model: CA706 requires bits 4-5 clear, and
               bit 6 only picks between two branches that converge again. */
            ret = 0x00;
            break;

        default:
            /* 0x1B0 and 0x1B1 are write-only; the ROM never reads them. */
            break;
    }

    return ret;
}

static void *
kbc3270_init(UNUSED(const device_t *info))
{
    kbc3270_t *dev = calloc(1, sizeof(kbc3270_t));

    if (dev == NULL)
        return NULL;

    io_sethandler(0x01b0, 0x0004, kbc3270_in, NULL, NULL,
                  kbc3270_out, NULL, NULL, dev);

    /* The on-card 8254.  The ROM never reads a counter back -- it only needs
       an interrupt inside the window it opens at CA6FB -- so all this has to
       do is expire once.  Counter 1 at the stock 1.193182 MHz input fires
       about 3 ms after it is loaded, comfortably inside that window.
       Counters 0 and 2 stay gated off: nothing observes counter 0, and the
       count of 4 the ROM gives it would schedule a host timer ~300000 times
       a second for no reason.
       ponytail: if real hardware turns out to need the ~12 ms the ROM's
       window is centred on, the knob is pitf_set_pit_const(). */
    dev->pit = device_add(&i8254_ext_io_fast_device);
    if (dev->pit != NULL) {
        /* Counters power up with the gate low, which disarms the timer at
           load time; counter 1 has to be let go explicitly. */
        pitf_ctr_set_gate(dev->pit, 1, 1);
        pitf_ctr_set_out_func(dev->pit, 1, kbc3270_timer_out);
        pitf_handler(1, 0x01b4, 0x0004, dev->pit);
    }

    return dev;
}

static void
kbc3270_close(void *priv)
{
    /* The device system closes the PIT we added. */
    free(priv);
}

const device_t kbc_3270pc_device = {
    .name          = "IBM 3270 PC Keyboard Adapter",
    .internal_name = "kbc_3270pc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = kbc3270_init,
    .close         = kbc3270_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
