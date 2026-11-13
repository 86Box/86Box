/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- hardware-assisted I2C master: I2C_CNTL_0,
 *          I2C_CNTL_1 and I2C_DATA.
 *
 *          The chip carries a small I2C bus master for multimedia parts
 *          such as video decoders and TV tuners. A client loads the slave
 *          address and any data bytes into a 16-entry buffer through
 *          I2C_DATA, sets the byte counts and the enable in I2C_CNTL_1,
 *          and writes I2C_CNTL_0 with I2C_GO; it then polls I2C_CNTL_0
 *          until I2C_GO reads 0 and one of I2C_DONE, I2C_NACK or I2C_HALT
 *          is set (Programmer's Guide Supplement, Video Capture: Hardware
 *          Assisted I2C). The sample code there drives it that way, and so
 *          does the Radeon's identical block in the Linux DRM driver
 *          (linux radeon_i2c.c r100_hw_i2c_xfer).
 *
 *          The master is wired to its own bus, an 86Box I2C bus with no
 *          device on it: the board has no tuner, decoder or other
 *          multimedia part. So every transfer that sends a byte ends the
 *          way it ends on a card with nothing fitted: the address byte is
 *          not acknowledged, I2C_NACK is set and I2C_DONE stays 0. A
 *          client that polls the status takes that to mean no part
 *          answers at that address. Monitor DDC is a separate bus,
 *          bit-banged through GPIO_MONID, and does not pass through here.
 *          A future multimedia part attaches to the bus with
 *          i2c_sethandler and needs no change in this file.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Register Reference Supplement,
 *              Multimedia Registers", 1999: the I2C_CNTL_0, I2C_CNTL_1 and
 *              I2C_DATA pages, the only field-level description of the
 *              block. Cited as "Multimedia Registers: <register>".
 *
 *          [3] ATI Technologies, "RAGE 128 Programmer's Guide Supplement,
 *              Video Capture", 1999, section 8.2, Inter IC Bus (I2C)
 *              Programming: the programming sequence. Cited as
 *              "Video Capture: <section title>".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/i2c.h>
#include <86box/mem.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

/* Push one byte into the buffer at the write pointer. The buffer is 16
   entries deep and both pointers advance by one per access and wrap
   (Multimedia Registers: I2C_CNTL_1, the I2C_DATA_COUNT description). */
static void
i2c_push(rage128_t *dev, uint8_t b)
{
    dev->i2c_buf[dev->i2c_wptr] = b;
    dev->i2c_wptr               = (dev->i2c_wptr + 1) & (RAGE128_I2C_BUF_SIZE - 1);
}

static uint8_t
i2c_pop(rage128_t *dev)
{
    uint8_t b = dev->i2c_buf[dev->i2c_rptr];

    dev->i2c_rptr = (dev->i2c_rptr + 1) & (RAGE128_I2C_BUF_SIZE - 1);
    return b;
}

/* Release the slave a transfer left addressed with a stop condition. */
static void
i2c_bus_stop(rage128_t *dev)
{
    if (dev->i2c_slave >= 0)
        i2c_stop(dev->mm_i2c, (uint8_t) dev->i2c_slave);
    dev->i2c_slave = -1;
}

/* Run one transfer to its end and return the status bit it ends with.
   The engine reads the bytes it sends from the buffer's read pointer:
   I2C_ADDR_COUNT address bytes, then for a transmit I2C_DATA_COUNT data
   bytes. For a receive it then reads I2C_DATA_COUNT bytes from the slave
   and appends them at the write pointer, so after an I2C_SOFT_RST and one
   address byte the client's first I2C_DATA read returns the first byte
   received, which is where a client expects it. The guide does not
   say where received bytes land; this placement is modeled.

   With I2C_START set, the first byte is the slave address with the
   read/write bit in bit 0 and the engine sends a start condition before
   it. When the previous transfer ended without I2C_STOP, the bus is
   still held and that start is a repeated start: no stop goes out
   first, which is how a client turns a register-address write into a
   read of that register. Without I2C_START the bytes continue the
   transaction the previous transfer left open. A byte
   nobody acknowledges ends the transfer at once with I2C_NACK (Video
   Capture: Hardware Assisted I2C, "Either the slave doesn't acknowledge,
   timeout, or the I2C_ABORT field is asserted, will terminate the
   transfer right away"), and the engine then releases the bus. Whether
   I2C_DONE is also set then is not documented; the Radeon's identical
   block treats "I2C_GO low with I2C_DONE clear" as a failed transfer
   (linux radeon_i2c.c r100_hw_i2c_xfer), so it is left clear. No slave
   holds the clock low, so I2C_HALT, the time-out, is never set. A
   transfer with no byte to send has no acknowledge to miss and ends
   with I2C_DONE; that case is not documented and is modeled. */
static uint32_t
i2c_transfer(rage128_t *dev)
{
    uint32_t c0     = dev->i2c_cntl_0;
    uint32_t c1     = dev->i2c_cntl_1;
    int      rx     = !!(c0 & RAGE128_I2C_RECEIVE);
    uint32_t n_addr = RAGE128_I2C_ADDR_COUNT(c1);
    uint32_t n_data = RAGE128_I2C_DATA_COUNT(c1);
    uint32_t n_send = n_addr + (rx ? 0 : n_data);
    uint32_t first  = 0x100;

    for (uint32_t i = 0; i < n_send; i++) {
        uint8_t b = i2c_pop(dev);
        int     ack;

        if (i == 0)
            first = b;
        if (i == 0 && (c0 & RAGE128_I2C_START)) {
            /* A slave on an 86Box bus hears only its own address. One
               that is still addressed when the start names another
               address would drop out at that address byte on the wire;
               here it is sent a stop (modeled). */
            if (dev->i2c_slave >= 0 && dev->i2c_slave != (b >> 1))
                i2c_bus_stop(dev);
            ack = i2c_start(dev->mm_i2c, b >> 1, b & 1);
            if (ack)
                dev->i2c_slave = b >> 1;
        } else
            ack = (dev->i2c_slave >= 0) && i2c_write(dev->mm_i2c, (uint8_t) dev->i2c_slave, b);
        if (!ack) {
            i2c_bus_stop(dev);
            rage128_log("RAGE128 I2C: %s %02x, byte %u of %u not acknowledged\n",
                        rx ? "receive" : "transmit", first, i + 1, n_send);
            return RAGE128_I2C_NACK;
        }
    }
    if (rx)
        for (uint32_t i = 0; i < n_data; i++)
            i2c_push(dev, (dev->i2c_slave >= 0)
                              ? i2c_read(dev->mm_i2c, (uint8_t) dev->i2c_slave)
                              : 0xff);
    if (c0 & RAGE128_I2C_STOP)
        i2c_bus_stop(dev);
    rage128_log("RAGE128 I2C: %s %02x, %u byte(s): done\n",
                rx ? "receive" : "transmit", first & 0xff, rx ? n_data : n_send);
    return RAGE128_I2C_DONE;
}

/* A write of I2C_CNTL_0. Returns 1 when the write ended a transfer in a
   way that raises I2C_INT.

   - I2C_DONE, I2C_NACK and I2C_HALT clear on a written 1 and keep their
     value on a written 0. The guide marks them read-only; clearing them
     by writing 1 is the first step of the documented sequence (Video
     Capture: Hardware Assisted I2C).
   - I2C_SOFT_RST resets the controller (Multimedia Registers:
     I2C_CNTL_0) at the write itself and is not kept. Every client loads
     the buffer right after writing it, which works only if the reset
     does not persist; it returns both buffer pointers to entry 0 and
     releases the bus. Reading it as 0 is modeled.
   - I2C_DRIVE_EN through I2C_ABORT and I2C_PRESCALE are kept as written.
     The pads and the clock are not modeled, so I2C_DRIVE_EN,
     I2C_DRIVE_SEL and I2C_PRESCALE have no other effect.
   - I2C_GO written 1 starts a transfer. The transfer completes inside
     this write, so I2C_GO always reads 0: on the chip it reads 1 while
     the bus is busy (Video Capture: Hardware Assisted I2C), for about
     ten clock periods of an address that nobody acknowledges, but every
     known client waits for it to fall before it reads the status, as the
     write sample in the same section does. Completing at the write is
     modeled.
   - I2C_ABORT written 1 ends the current operation by sending a stop
     and lets I2C_GO fall (Multimedia Registers: I2C_CNTL_0). No transfer
     is ever in flight, so the stop releases a slave that an earlier
     transfer left addressed, and a write carrying both I2C_ABORT and
     I2C_GO, which a client may send to stop the bus, starts nothing.
   - I2C_GO with I2C_EN clear in I2C_CNTL_1 starts nothing and leaves the
     status alone; the guide does not say what the engine does, and this
     is modeled. */
static int
i2c_write_cntl_0(rage128_t *dev, uint32_t val, uint32_t mask)
{
    uint32_t w = val & mask;
    uint32_t st;

    dev->i2c_cntl_0 &= ~(w & RAGE128_I2C_STATUS);
    if (w & RAGE128_I2C_SOFT_RST) {
        dev->i2c_wptr = 0;
        dev->i2c_rptr = 0;
        i2c_bus_stop(dev);
    }
    dev->i2c_cntl_0 = (dev->i2c_cntl_0 & ~(mask & RAGE128_I2C_CNTL_0_LATCH))
        | (w & RAGE128_I2C_CNTL_0_LATCH);

    if (w & RAGE128_I2C_ABORT) {
        i2c_bus_stop(dev);
        return 0;
    }
    if (!(w & RAGE128_I2C_GO))
        return 0;
    if (!(dev->i2c_cntl_1 & RAGE128_I2C_EN)) {
        rage128_log("RAGE128 I2C: GO with I2C_EN clear, nothing sent\n");
        return 0;
    }
    dev->i2c_cntl_0 &= ~RAGE128_I2C_STATUS;
    st = i2c_transfer(dev);
    dev->i2c_cntl_0 |= st;
    return !!(st & (RAGE128_I2C_NACK | RAGE128_I2C_HALT));
}

/* Register reads. I2C_DATA reads the entry at the read pointer in bits
   7:0 and advances the pointer (Multimedia Registers: I2C_DATA,
   I2C_CNTL_1). Each CPU access reaches this once, whatever its width, so
   a byte read and a dword read each take one entry. */
int
rage128_i2c_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    switch (off) {
        case RAGE128_I2C_CNTL_0:
            thread_wait_mutex(dev->i2c_mtx);
            *val = dev->i2c_cntl_0;
            thread_release_mutex(dev->i2c_mtx);
            return 1;
        case RAGE128_I2C_CNTL_1:
            thread_wait_mutex(dev->i2c_mtx);
            *val = dev->i2c_cntl_1;
            thread_release_mutex(dev->i2c_mtx);
            return 1;
        case RAGE128_I2C_DATA:
            thread_wait_mutex(dev->i2c_mtx);
            *val = i2c_pop(dev);
            thread_release_mutex(dev->i2c_mtx);
            return 1;
        default:
            return 0;
    }
}

/* Register writes. A write to I2C_DATA that includes byte 0 pushes that
   byte; bits 31:8 are reserved. I2C_CNTL_1 keeps its documented fields.

   Locking: every engine field is changed under i2c_mtx, because a type-0
   or type-1 ring packet can write these registers on the CCE thread
   while the CPU thread polls them. The interrupt is raised after the
   mutex is released, the way every CCE-reachable status source does it:
   the status bit is set with one atomic step and rage128_gen_int_update
   drives INTA on the CPU thread or, on the CCE thread, only marks the
   line stale for the CPU thread to pick up. So the engine never holds
   its mutex while it calls into the interrupt block, and the interrupt
   block takes no lock of its own. A slave on the bus is called with the
   mutex held and must not access these registers. */
int
rage128_i2c_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    int irq = 0;

    switch (off) {
        case RAGE128_I2C_CNTL_0:
            thread_wait_mutex(dev->i2c_mtx);
            irq = i2c_write_cntl_0(dev, val, mask);
            thread_release_mutex(dev->i2c_mtx);
            break;
        case RAGE128_I2C_CNTL_1:
            thread_wait_mutex(dev->i2c_mtx);
            dev->i2c_cntl_1 = ((dev->i2c_cntl_1 & ~mask) | (val & mask)) & RAGE128_I2C_CNTL_1_MASK;
            thread_release_mutex(dev->i2c_mtx);
            break;
        case RAGE128_I2C_DATA:
            if (mask & 0xff) {
                thread_wait_mutex(dev->i2c_mtx);
                i2c_push(dev, (uint8_t) val);
                thread_release_mutex(dev->i2c_mtx);
            }
            break;
        default:
            return 0;
    }

    /* I2C_INT, GEN_INT_STATUS bit 17 (RRG: GEN_INT_STATUS, p. 3-195 /
       PDF 213), with its enable in GEN_INT_CNTL bit 17 (RRG:
       GEN_INT_CNTL, p. 3-193 / PDF 211). The supplement issues the
       interrupt when a slave does not acknowledge, on a time-out and on
       an abort (Video Capture: Hardware Assisted I2C); it does not say a
       normal completion interrupts, so that does not. */
    if (irq) {
        atomic_fetch_or(&dev->gen_int_status, RAGE128_GIS_I2C);
        rage128_gen_int_update(dev);
    }
    return 1;
}

/* All three registers reset to 0 (Multimedia Registers: I2C_CNTL_0,
   I2C_CNTL_1, I2C_DATA); the buffer is cleared with them. */
void
rage128_i2c_reset(rage128_t *dev)
{
    thread_wait_mutex(dev->i2c_mtx);
    i2c_bus_stop(dev);
    dev->i2c_cntl_0 = 0x00000000;
    dev->i2c_cntl_1 = 0x00000000;
    memset(dev->i2c_buf, 0, sizeof(dev->i2c_buf));
    dev->i2c_wptr = 0;
    dev->i2c_rptr = 0;
    thread_release_mutex(dev->i2c_mtx);
}

void
rage128_i2c_init(rage128_t *dev)
{
    dev->i2c_mtx   = thread_create_mutex();
    dev->mm_i2c    = i2c_addbus("ati_rage128_mm_i2c");
    dev->i2c_slave = -1;
}

void
rage128_i2c_close(rage128_t *dev)
{
    i2c_bus_stop(dev);
    i2c_removebus(dev->mm_i2c);
    dev->mm_i2c = NULL;
    thread_close_mutex(dev->i2c_mtx);
    dev->i2c_mtx = NULL;
}
