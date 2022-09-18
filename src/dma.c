/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel DMA controllers.
 *
 *
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>

dma_t   dma[8];
uint8_t dma_e;
uint8_t dma_m;

static uint8_t  dmaregs[3][16];
static int      dma_wp[2];
static uint8_t  dma_stat;
static uint8_t  dma_stat_rq;
static uint8_t  dma_stat_rq_pc;
static uint8_t  dma_command[2];
static uint8_t  dma_req_is_soft;
static uint8_t  dma_advanced;
static uint8_t  dma_at;
static uint8_t  dma_buffer[65536];
static uint16_t dma_sg_base;
static uint16_t dma16_buffer[65536];
static uint32_t dma_mask;

static struct {
    int xfr_command,
        xfr_channel;
    int byte_ptr;

    int is_ps2;
} dma_ps2;

#define DMA_PS2_IOA            (1 << 0)
#define DMA_PS2_AUTOINIT       (1 << 1)
#define DMA_PS2_XFER_MEM_TO_IO (1 << 2)
#define DMA_PS2_XFER_IO_TO_MEM (3 << 2)
#define DMA_PS2_XFER_MASK      (3 << 2)
#define DMA_PS2_DEC2           (1 << 4)
#define DMA_PS2_SIZE16         (1 << 6)

#ifdef ENABLE_DMA_LOG
int dma_do_log = ENABLE_DMA_LOG;

static void
dma_log(const char *fmt, ...)
{
    va_list ap;

    if (dma_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dma_log(fmt, ...)
#endif

static void dma_ps2_run(int channel);

int
dma_get_drq(int channel)
{
    return !!(dma_stat_rq_pc & (1 << channel));
}

void
dma_set_drq(int channel, int set)
{
    dma_stat_rq_pc &= ~(1 << channel);
    if (set)
        dma_stat_rq_pc |= (1 << channel);
}

static int
dma_transfer_size(dma_t *dev)
{
    return dev->transfer_mode & 0xff;
}

static void
dma_sg_next_addr(dma_t *dev)
{
    int ts = dma_transfer_size(dev);

    dma_bm_read(dev->ptr_cur, (uint8_t *) &(dev->addr), 4, ts);
    dma_bm_read(dev->ptr_cur + 4, (uint8_t *) &(dev->count), 4, ts);
    dma_log("DMA S/G DWORDs: %08X %08X\n", dev->addr, dev->count);
    dev->eot = dev->count >> 31;
    dev->count &= 0xfffe;
    dev->cb = (uint16_t) dev->count;
    dev->cc = (int) dev->count;
    if (!dev->count)
        dev->count = 65536;
    if (ts == 2)
        dev->addr &= 0xfffffffe;
    dev->ab   = dev->addr & dma_mask;
    dev->ac   = dev->addr & dma_mask;
    dev->page = dev->page_l = (dev->ac >> 16) & 0xff;
    dev->page_h             = (dev->ac >> 24) & 0xff;
    dev->ptr_cur += 8;
}

static void
dma_block_transfer(int channel)
{
    int i, bit16;

    bit16 = (channel >= 4);

    if (dma_advanced)
        bit16 = !!(dma_transfer_size(&(dma[channel])) == 2);

    dma_req_is_soft = 1;
    for (i = 0; i <= dma[channel].cb; i++) {
        if ((dma[channel].mode & 0x8c) == 0x84) {
            if (bit16)
                dma_channel_write(channel, dma16_buffer[i]);
            else
                dma_channel_write(channel, dma_buffer[i]);
        } else if ((dma[channel].mode & 0x8c) == 0x88) {
            if (bit16)
                dma16_buffer[i] = dma_channel_read(channel);
            else
                dma_buffer[i] = dma_channel_read(channel);
        }
    }
    dma_req_is_soft = 0;
}

static void
dma_mem_to_mem_transfer(void)
{
    int i;

    if ((dma[0].mode & 0x0c) != 0x08)
        fatal("DMA memory to memory transfer: channel 0 mode not read\n");
    if ((dma[1].mode & 0x0c) != 0x04)
        fatal("DMA memory to memory transfer: channel 1 mode not write\n");

    dma_req_is_soft = 1;

    for (i = 0; i <= dma[0].cb; i++)
        dma_buffer[i] = dma_channel_read(0);

    for (i = 0; i <= dma[1].cb; i++)
        dma_channel_write(1, dma_buffer[i]);

    dma_req_is_soft = 0;
}

static void
dma_sg_write(uint16_t port, uint8_t val, void *priv)
{
    dma_t *dev = (dma_t *) priv;

    dma_log("DMA S/G BYTE  write: %04X       %02X\n", port, val);

    port &= 0xff;

    if (port < 0x20)
        port &= 0xf8;
    else
        port &= 0xe3;

    switch (port) {
        case 0x00:
            dma_log("DMA S/G Cmd   : val = %02X, old = %02X\n", val, dev->sg_command);
            if ((val & 1) && !(dev->sg_command & 1)) { /*Start*/
#ifdef ENABLE_DMA_LOG
                dma_log("DMA S/G start\n");
#endif
                dev->ptr_cur = dev->ptr;
                dma_sg_next_addr(dev);
                dev->sg_status = (dev->sg_status & 0xf7) | 0x01;
            }
            if (!(val & 1) && (dev->sg_command & 1)) { /*Stop*/
#ifdef ENABLE_DMA_LOG
                dma_log("DMA S/G stop\n");
#endif
                dev->sg_status &= ~0x81;
            }

            dev->sg_command = val;
            break;
        case 0x20:
            dev->ptr = (dev->ptr & 0xffffff00) | (val & 0xfc);
            dev->ptr %= (mem_size * 1024);
            dev->ptr0 = val;
            break;
        case 0x21:
            dev->ptr = (dev->ptr & 0xffff00fc) | (val << 8);
            dev->ptr %= (mem_size * 1024);
            break;
        case 0x22:
            dev->ptr = (dev->ptr & 0xff00fffc) | (val << 16);
            dev->ptr %= (mem_size * 1024);
            break;
        case 0x23:
            dev->ptr = (dev->ptr & 0x00fffffc) | (val << 24);
            dev->ptr %= (mem_size * 1024);
            break;
    }
}

static void
dma_sg_writew(uint16_t port, uint16_t val, void *priv)
{
    dma_t *dev = (dma_t *) priv;

    dma_log("DMA S/G WORD  write: %04X     %04X\n", port, val);

    port &= 0xff;

    if (port < 0x20)
        port &= 0xf8;
    else
        port &= 0xe3;

    switch (port) {
        case 0x00:
            dma_sg_write(port, val & 0xff, priv);
            break;
        case 0x20:
            dev->ptr = (dev->ptr & 0xffff0000) | (val & 0xfffc);
            dev->ptr %= (mem_size * 1024);
            dev->ptr0 = val & 0xff;
            break;
        case 0x22:
            dev->ptr = (dev->ptr & 0x0000fffc) | (val << 16);
            dev->ptr %= (mem_size * 1024);
            break;
    }
}

static void
dma_sg_writel(uint16_t port, uint32_t val, void *priv)
{
    dma_t *dev = (dma_t *) priv;

    dma_log("DMA S/G DWORD write: %04X %08X\n", port, val);

    port &= 0xff;

    if (port < 0x20)
        port &= 0xf8;
    else
        port &= 0xe3;

    switch (port) {
        case 0x00:
            dma_sg_write(port, val & 0xff, priv);
            break;
        case 0x20:
            dev->ptr = (val & 0xfffffffc);
            dev->ptr %= (mem_size * 1024);
            dev->ptr0 = val & 0xff;
            break;
    }
}

static uint8_t
dma_sg_read(uint16_t port, void *priv)
{
    dma_t *dev = (dma_t *) priv;

    uint8_t ret = 0xff;

    port &= 0xff;

    if (port < 0x20)
        port &= 0xf8;
    else
        port &= 0xe3;

    switch (port) {
        case 0x08:
            ret = (dev->sg_status & 0x01);
            if (dev->eot)
                ret |= 0x80;
            if ((dev->sg_command & 0xc0) == 0x40)
                ret |= 0x20;
            if (dev->ab != 0x00000000)
                ret |= 0x08;
            if (dev->ac != 0x00000000)
                ret |= 0x04;
            break;
        case 0x20:
            ret = dev->ptr0;
            break;
        case 0x21:
            ret = dev->ptr >> 8;
            break;
        case 0x22:
            ret = dev->ptr >> 16;
            break;
        case 0x23:
            ret = dev->ptr >> 24;
            break;
    }

    dma_log("DMA S/G BYTE  read : %04X       %02X\n", port, ret);

    return ret;
}

static uint16_t
dma_sg_readw(uint16_t port, void *priv)
{
    dma_t *dev = (dma_t *) priv;

    uint16_t ret = 0xffff;

    port &= 0xff;

    if (port < 0x20)
        port &= 0xf8;
    else
        port &= 0xe3;

    switch (port) {
        case 0x08:
            ret = (uint16_t) dma_sg_read(port, priv);
            break;
        case 0x20:
            ret = dev->ptr0 | (dev->ptr & 0xff00);
            break;
        case 0x22:
            ret = dev->ptr >> 16;
            break;
    }

    dma_log("DMA S/G WORD  read : %04X     %04X\n", port, ret);

    return ret;
}

static uint32_t
dma_sg_readl(uint16_t port, void *priv)
{
    dma_t *dev = (dma_t *) priv;

    uint32_t ret = 0xffffffff;

    port &= 0xff;

    if (port < 0x20)
        port &= 0xf8;
    else
        port &= 0xe3;

    switch (port) {
        case 0x08:
            ret = (uint32_t) dma_sg_read(port, priv);
            break;
        case 0x20:
            ret = dev->ptr0 | (dev->ptr & 0xffffff00);
            break;
    }

    dma_log("DMA S/G DWORD read : %04X %08X\n", port, ret);

    return ret;
}

static void
dma_ext_mode_write(uint16_t addr, uint8_t val, void *priv)
{
    int channel = (val & 0x03);

    if (addr == 0x4d6)
        channel |= 4;

    dma[channel].ext_mode = val & 0x7c;

    switch ((val > 2) & 0x03) {
        case 0x00:
            dma[channel].transfer_mode = 0x0101;
            break;
        case 0x01:
            dma[channel].transfer_mode = 0x0202;
            break;
        case 0x02: /* 0x02 is reserved. */
            /* Logic says this should be an undocumented mode that counts by words,
               but is 8-bit I/O, thus only transferring every second byte. */
            dma[channel].transfer_mode = 0x0201;
            break;
        case 0x03:
            dma[channel].transfer_mode = 0x0102;
            break;
    }
}

static uint8_t
dma_sg_int_status_read(uint16_t addr, void *priv)
{
    int     i;
    uint8_t ret = 0x00;

    for (i = 0; i < 8; i++) {
        if (i != 4)
            ret = (!!(dma[i].sg_status & 8)) << i;
    }

    return ret;
}

static uint8_t
dma_read(uint16_t addr, void *priv)
{
    int     channel = (addr >> 1) & 3;
    uint8_t temp;

    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[0] ^= 1;
            if (dma_wp[0])
                return (dma[channel].ac & 0xff);
            return ((dma[channel].ac >> 8) & 0xff);

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[0] ^= 1;
            if (dma_wp[0])
                temp = dma[channel].cc & 0xff;
            else
                temp = dma[channel].cc >> 8;
            return (temp);

        case 8: /*Status register*/
            temp = dma_stat_rq_pc & 0xf;
            temp <<= 4;
            temp |= dma_stat & 0xf;
            dma_stat &= ~0xf;
            return (temp);

        case 0xd: /*Temporary register*/
            return (0);
    }

    return (dmaregs[0][addr & 0xf]);
}

static void
dma_write(uint16_t addr, uint8_t val, void *priv)
{
    int channel = (addr >> 1) & 3;

    dmaregs[0][addr & 0xf] = val;
    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[0] ^= 1;
            if (dma_wp[0])
                dma[channel].ab = (dma[channel].ab & 0xffffff00 & dma_mask) | val;
            else
                dma[channel].ab = (dma[channel].ab & 0xffff00ff & dma_mask) | (val << 8);
            dma[channel].ac = dma[channel].ab;
            return;

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[0] ^= 1;
            if (dma_wp[0])
                dma[channel].cb = (dma[channel].cb & 0xff00) | val;
            else
                dma[channel].cb = (dma[channel].cb & 0x00ff) | (val << 8);
            dma[channel].cc = dma[channel].cb;
            return;

        case 8: /*Control register*/
            dma_command[0] = val;
            if (val & 0x01)
                pclog("[%08X:%04X] Memory-to-memory enable\n", CS, cpu_state.pc);
            return;

        case 9: /*Request register */
            channel = (val & 3);
            if (val & 4) {
                dma_stat_rq_pc |= (1 << channel);
                if ((channel == 0) && (dma_command[0] & 0x01)) {
                    pclog("Memory to memory transfer start\n");
                    dma_mem_to_mem_transfer();
                } else
                    dma_block_transfer(channel);
            } else
                dma_stat_rq_pc &= ~(1 << channel);
            break;

        case 0xa: /*Mask*/
            channel = (val & 3);
            if (val & 4)
                dma_m |= (1 << channel);
            else
                dma_m &= ~(1 << channel);
            return;

        case 0xb: /*Mode*/
            channel           = (val & 3);
            dma[channel].mode = val;
            if (dma_ps2.is_ps2) {
                dma[channel].ps2_mode &= ~0x1c;
                if (val & 0x20)
                    dma[channel].ps2_mode |= 0x10;
                if ((val & 0xc) == 8)
                    dma[channel].ps2_mode |= 4;
                else if ((val & 0xc) == 4)
                    dma[channel].ps2_mode |= 0xc;
            }
            return;

        case 0xc: /*Clear FF*/
            dma_wp[0] = 0;
            return;

        case 0xd: /*Master clear*/
            dma_wp[0] = 0;
            dma_m |= 0xf;
            dma_stat_rq_pc &= ~0x0f;
            return;

        case 0xe: /*Clear mask*/
            dma_m &= 0xf0;
            return;

        case 0xf: /*Mask write*/
            dma_m = (dma_m & 0xf0) | (val & 0xf);
            return;
    }
}

static uint8_t
dma_ps2_read(uint16_t addr, void *priv)
{
    dma_t  *dma_c = &dma[dma_ps2.xfr_channel];
    uint8_t temp  = 0xff;

    switch (addr) {
        case 0x1a:
            switch (dma_ps2.xfr_command) {
                case 2: /*Address*/
                case 3:
                    switch (dma_ps2.byte_ptr) {
                        case 0:
                            temp             = dma_c->ac & 0xff;
                            dma_ps2.byte_ptr = 1;
                            break;
                        case 1:
                            temp             = (dma_c->ac >> 8) & 0xff;
                            dma_ps2.byte_ptr = 2;
                            break;
                        case 2:
                            temp             = (dma_c->ac >> 16) & 0xff;
                            dma_ps2.byte_ptr = 0;
                            break;
                    }
                    break;

                case 4: /*Count*/
                case 5:
                    if (dma_ps2.byte_ptr)
                        temp = dma_c->cc >> 8;
                    else
                        temp = dma_c->cc & 0xff;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;

                case 6: /*Read DMA status*/
                    if (dma_ps2.byte_ptr) {
                        temp = ((dma_stat_rq & 0xf0) >> 4) | (dma_stat & 0xf0);
                        dma_stat &= ~0xf0;
                        dma_stat_rq &= ~0xf0;
                    } else {
                        temp = (dma_stat_rq & 0xf) | ((dma_stat & 0xf) << 4);
                        dma_stat &= ~0xf;
                        dma_stat_rq &= ~0xf;
                    }
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;

                case 7: /*Mode*/
                    temp = dma_c->ps2_mode;
                    break;

                case 8: /*Arbitration Level*/
                    temp = dma_c->arb_level;
                    break;

                default:
                    fatal("Bad XFR Read command %i channel %i\n", dma_ps2.xfr_command, dma_ps2.xfr_channel);
            }
            break;
    }
    return (temp);
}

static void
dma_ps2_write(uint16_t addr, uint8_t val, void *priv)
{
    dma_t  *dma_c = &dma[dma_ps2.xfr_channel];
    uint8_t mode;

    switch (addr) {
        case 0x18:
            dma_ps2.xfr_channel = val & 0x7;
            dma_ps2.xfr_command = val >> 4;
            dma_ps2.byte_ptr    = 0;
            switch (dma_ps2.xfr_command) {
                case 9: /*Set DMA mask*/
                    dma_m |= (1 << dma_ps2.xfr_channel);
                    break;

                case 0xa: /*Reset DMA mask*/
                    dma_m &= ~(1 << dma_ps2.xfr_channel);
                    break;

                case 0xb:
                    if (!(dma_m & (1 << dma_ps2.xfr_channel)))
                        dma_ps2_run(dma_ps2.xfr_channel);
                    break;
            }
            break;

        case 0x1a:
            switch (dma_ps2.xfr_command) {
                case 0: /*I/O address*/
                    if (dma_ps2.byte_ptr)
                        dma_c->io_addr = (dma_c->io_addr & 0x00ff) | (val << 8);
                    else
                        dma_c->io_addr = (dma_c->io_addr & 0xff00) | val;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;

                case 2: /*Address*/
                    switch (dma_ps2.byte_ptr) {
                        case 0:
                            dma_c->ac        = (dma_c->ac & 0xffff00) | val;
                            dma_ps2.byte_ptr = 1;
                            break;

                        case 1:
                            dma_c->ac        = (dma_c->ac & 0xff00ff) | (val << 8);
                            dma_ps2.byte_ptr = 2;
                            break;

                        case 2:
                            dma_c->ac        = (dma_c->ac & 0x00ffff) | (val << 16);
                            dma_ps2.byte_ptr = 0;
                            break;
                    }
                    dma_c->ab = dma_c->ac;
                    break;

                case 4: /*Count*/
                    if (dma_ps2.byte_ptr)
                        dma_c->cc = (dma_c->cc & 0xff) | (val << 8);
                    else
                        dma_c->cc = (dma_c->cc & 0xff00) | val;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    dma_c->cb        = dma_c->cc;
                    break;

                case 7: /*Mode register*/
                    mode = 0;
                    if (val & DMA_PS2_DEC2)
                        mode |= 0x20;
                    if ((val & DMA_PS2_XFER_MASK) == DMA_PS2_XFER_MEM_TO_IO)
                        mode |= 8;
                    else if ((val & DMA_PS2_XFER_MASK) == DMA_PS2_XFER_IO_TO_MEM)
                        mode |= 4;
                    dma_c->mode = (dma_c->mode & ~0x2c) | mode;
                    if (val & DMA_PS2_AUTOINIT)
                        dma_c->mode |= 0x10;
                    dma_c->ps2_mode = val;
                    dma_c->size     = val & DMA_PS2_SIZE16;
                    break;

                case 8: /*Arbitration Level*/
                    dma_c->arb_level = val;
                    break;

                default:
                    fatal("Bad XFR command %i channel %i val %02x\n", dma_ps2.xfr_command, dma_ps2.xfr_channel, val);
            }
            break;
    }
}

static uint8_t
dma16_read(uint16_t addr, void *priv)
{
    int     channel = ((addr >> 2) & 3) + 4;
    uint8_t temp;

    addr >>= 1;
    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[1] ^= 1;
            if (dma_ps2.is_ps2) {
                if (dma_wp[1])
                    return (dma[channel].ac);
                return ((dma[channel].ac >> 8) & 0xff);
            }
            if (dma_wp[1])
                return ((dma[channel].ac >> 1) & 0xff);
            return ((dma[channel].ac >> 9) & 0xff);

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[1] ^= 1;
            if (dma_wp[1])
                temp = dma[channel].cc & 0xff;
            else
                temp = dma[channel].cc >> 8;
            return (temp);

        case 8: /*Status register*/
            temp = (dma_stat_rq_pc & 0xf0);
            temp |= dma_stat >> 4;
            dma_stat &= ~0xf0;
            return (temp);
    }

    return (dmaregs[1][addr & 0xf]);
}

static void
dma16_write(uint16_t addr, uint8_t val, void *priv)
{
    int channel = ((addr >> 2) & 3) + 4;
    addr >>= 1;

    dmaregs[1][addr & 0xf] = val;
    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[1] ^= 1;
            if (dma_ps2.is_ps2) {
                if (dma_wp[1])
                    dma[channel].ab = (dma[channel].ab & 0xffffff00 & dma_mask) | val;
                else
                    dma[channel].ab = (dma[channel].ab & 0xffff00ff & dma_mask) | (val << 8);
            } else {
                if (dma_wp[1])
                    dma[channel].ab = (dma[channel].ab & 0xfffffe00 & dma_mask) | (val << 1);
                else
                    dma[channel].ab = (dma[channel].ab & 0xfffe01ff & dma_mask) | (val << 9);
            }
            dma[channel].ac = dma[channel].ab;
            return;

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[1] ^= 1;
            if (dma_wp[1])
                dma[channel].cb = (dma[channel].cb & 0xff00) | val;
            else
                dma[channel].cb = (dma[channel].cb & 0x00ff) | (val << 8);
            dma[channel].cc = dma[channel].cb;
            return;

        case 8: /*Control register*/
            return;

        case 9: /*Request register */
            channel = (val & 3) + 4;
            if (val & 4) {
                dma_stat_rq_pc |= (1 << channel);
                dma_block_transfer(channel);
            } else
                dma_stat_rq_pc &= ~(1 << channel);
            break;

        case 0xa: /*Mask*/
            channel = (val & 3);
            if (val & 4)
                dma_m |= (0x10 << channel);
            else
                dma_m &= ~(0x10 << channel);
            return;

        case 0xb: /*Mode*/
            channel           = (val & 3) + 4;
            dma[channel].mode = val;
            if (dma_ps2.is_ps2) {
                dma[channel].ps2_mode &= ~0x1c;
                if (val & 0x20)
                    dma[channel].ps2_mode |= 0x10;
                if ((val & 0xc) == 8)
                    dma[channel].ps2_mode |= 4;
                else if ((val & 0xc) == 4)
                    dma[channel].ps2_mode |= 0xc;
            }
            return;

        case 0xc: /*Clear FF*/
            dma_wp[1] = 0;
            return;

        case 0xd: /*Master clear*/
            dma_wp[1] = 0;
            dma_m |= 0xf0;
            dma_stat_rq_pc &= ~0xf0;
            return;

        case 0xe: /*Clear mask*/
            dma_m &= 0x0f;
            return;

        case 0xf: /*Mask write*/
            dma_m = (dma_m & 0x0f) | ((val & 0xf) << 4);
            return;
    }
}

#define CHANNELS               \
    {                          \
        8, 2, 3, 1, 8, 8, 8, 0 \
    }

static void
dma_page_write(uint16_t addr, uint8_t val, void *priv)
{
    uint8_t convert[8] = CHANNELS;

#ifdef USE_DYNAREC
    if ((addr == 0x84) && cpu_use_dynarec)
        update_tsc();
#endif

    addr &= 0x0f;
    dmaregs[2][addr] = val;

    if (addr >= 8)
        addr = convert[addr & 0x07] | 4;
    else
        addr = convert[addr & 0x07];

    if (addr < 8) {
        dma[addr].page_l = val;

        if (addr > 4) {
            dma[addr].page = val & 0xfe;
            dma[addr].ab   = (dma[addr].ab & 0xff01ffff & dma_mask) | (dma[addr].page << 16);
            dma[addr].ac   = (dma[addr].ac & 0xff01ffff & dma_mask) | (dma[addr].page << 16);
        } else {
            dma[addr].page = (dma_at) ? val : val & 0xf;
            dma[addr].ab   = (dma[addr].ab & 0xff00ffff & dma_mask) | (dma[addr].page << 16);
            dma[addr].ac   = (dma[addr].ac & 0xff00ffff & dma_mask) | (dma[addr].page << 16);
        }
    }
}

static uint8_t
dma_page_read(uint16_t addr, void *priv)
{
    uint8_t convert[8] = CHANNELS;
    uint8_t ret        = 0xff;

    addr &= 0x0f;
    ret = dmaregs[2][addr];

    if (addr >= 8)
        addr = convert[addr & 0x07] | 4;
    else
        addr = convert[addr & 0x07];

    if (addr < 8)
        ret = dma[addr].page_l;

    return ret;
}

static void
dma_high_page_write(uint16_t addr, uint8_t val, void *priv)
{
    uint8_t convert[8] = CHANNELS;

    addr &= 0x0f;

    if (addr >= 8)
        addr = convert[addr & 0x07] | 4;
    else
        addr = convert[addr & 0x07];

    if (addr < 8) {
        dma[addr].page_h = val;

        dma[addr].ab = ((dma[addr].ab & 0xffffff) | (dma[addr].page << 24)) & dma_mask;
        dma[addr].ac = ((dma[addr].ac & 0xffffff) | (dma[addr].page << 24)) & dma_mask;
    }
}

static uint8_t
dma_high_page_read(uint16_t addr, void *priv)
{
    uint8_t convert[8] = CHANNELS;
    uint8_t ret        = 0xff;

    addr &= 0x0f;

    if (addr >= 8)
        addr = convert[addr & 0x07] | 4;
    else
        addr = convert[addr & 0x07];

    if (addr < 8)
        ret = dma[addr].page_h;

    return ret;
}

void
dma_set_params(uint8_t advanced, uint32_t mask)
{
    dma_advanced = advanced;
    dma_mask     = mask;
}

void
dma_set_mask(uint32_t mask)
{
    int i;

    dma_mask = mask;

    for (i = 0; i < 8; i++) {
        dma[i].ab &= mask;
        dma[i].ac &= mask;
    }
}

void
dma_set_at(uint8_t at)
{
    dma_at = at;
}

void
dma_reset(void)
{
    int c;

    dma_wp[0] = dma_wp[1] = 0;
    dma_m                 = 0;

    dma_e = 0xff;

    for (c = 0; c < 16; c++)
        dmaregs[0][c] = dmaregs[1][c] = 0;

    for (c = 0; c < 8; c++) {
        memset(&(dma[c]), 0x00, sizeof(dma_t));
        dma[c].size          = (c & 4) ? 1 : 0;
        dma[c].transfer_mode = (c & 4) ? 0x0202 : 0x0101;
    }

    dma_stat        = 0x00;
    dma_stat_rq     = 0x00;
    dma_stat_rq_pc  = 0x00;
    dma_req_is_soft = 0;
    dma_advanced    = 0;

    memset(dma_buffer, 0x00, sizeof(dma_buffer));
    memset(dma16_buffer, 0x00, sizeof(dma16_buffer));

    dma_remove_sg();
    dma_sg_base = 0x0400;

    dma_mask = 0x00ffffff;

    dma_at = is286;
}

void
dma_remove_sg(void)
{
    int i;

    io_removehandler(dma_sg_base + 0x0a, 0x01,
                     dma_sg_int_status_read, NULL, NULL,
                     NULL, NULL, NULL,
                     NULL);

    for (i = 0; i < 8; i++) {
        io_removehandler(dma_sg_base + 0x10 + i, 0x01,
                         dma_sg_read, dma_sg_readw, dma_sg_readl,
                         dma_sg_write, dma_sg_writew, dma_sg_writel,
                         &dma[i]);
        io_removehandler(dma_sg_base + 0x18 + i, 0x01,
                         dma_sg_read, dma_sg_readw, dma_sg_readl,
                         dma_sg_write, dma_sg_writew, dma_sg_writel,
                         &dma[i]);
        io_removehandler(dma_sg_base + 0x20 + i, 0x04,
                         dma_sg_read, dma_sg_readw, dma_sg_readl,
                         dma_sg_write, dma_sg_writew, dma_sg_writel,
                         &dma[i]);
    }
}

void
dma_set_sg_base(uint8_t sg_base)
{
    int i;

    dma_sg_base = sg_base << 8;

    io_sethandler(dma_sg_base + 0x0a, 0x01,
                  dma_sg_int_status_read, NULL, NULL,
                  NULL, NULL, NULL,
                  NULL);

    for (i = 0; i < 8; i++) {
        io_sethandler(dma_sg_base + 0x10 + i, 0x01,
                      dma_sg_read, dma_sg_readw, dma_sg_readl,
                      dma_sg_write, dma_sg_writew, dma_sg_writel,
                      &dma[i]);
        io_sethandler(dma_sg_base + 0x18 + i, 0x01,
                      dma_sg_read, dma_sg_readw, dma_sg_readl,
                      dma_sg_write, dma_sg_writew, dma_sg_writel,
                      &dma[i]);
        io_sethandler(dma_sg_base + 0x20 + i, 0x04,
                      dma_sg_read, dma_sg_readw, dma_sg_readl,
                      dma_sg_write, dma_sg_writew, dma_sg_writel,
                      &dma[i]);
    }
}

void
dma_ext_mode_init(void)
{
    io_sethandler(0x040b, 0x01,
                  NULL, NULL, NULL, dma_ext_mode_write, NULL, NULL, NULL);
    io_sethandler(0x04d6, 0x01,
                  NULL, NULL, NULL, dma_ext_mode_write, NULL, NULL, NULL);
}

void
dma_high_page_init(void)
{
    io_sethandler(0x0480, 8,
                  dma_high_page_read, NULL, NULL, dma_high_page_write, NULL, NULL, NULL);
}

void
dma_init(void)
{
    dma_reset();

    io_sethandler(0x0000, 16,
                  dma_read, NULL, NULL, dma_write, NULL, NULL, NULL);
    io_sethandler(0x0080, 8,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    dma_ps2.is_ps2 = 0;
}

void
dma16_init(void)
{
    dma_reset();

    io_sethandler(0x00C0, 32,
                  dma16_read, NULL, NULL, dma16_write, NULL, NULL, NULL);
    io_sethandler(0x0088, 8,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_set(void)
{
    io_sethandler(0x0090, 2,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0093, 13,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_set_piix(void)
{
    io_sethandler(0x0090, 1,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0094, 3,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0098, 1,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x009C, 3,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_remove(void)
{
    io_removehandler(0x0090, 2,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x0093, 13,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_remove_piix(void)
{
    io_removehandler(0x0090, 1,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x0094, 3,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x0098, 1,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x009C, 3,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
ps2_dma_init(void)
{
    dma_reset();

    io_sethandler(0x0018, 1,
                  dma_ps2_read, NULL, NULL, dma_ps2_write, NULL, NULL, NULL);
    io_sethandler(0x001a, 1,
                  dma_ps2_read, NULL, NULL, dma_ps2_write, NULL, NULL, NULL);
    dma_ps2.is_ps2 = 1;
}

extern void dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize);
extern void dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize);

static int
dma_sg(uint8_t *data, int transfer_length, int out, void *priv)
{
    dma_t *dev = (dma_t *) priv;
#ifdef ENABLE_DMA_LOG
    char *sop;
#endif

    int force_end = 0, buffer_pos = 0;

#ifdef ENABLE_DMA_LOG
    sop = out ? "Read" : "Writ";
#endif

    if (!(dev->sg_status & 1))
        return 2; /*S/G disabled*/

    dma_log("DMA S/G %s: %i bytes\n", out ? "write" : "read", transfer_length);

    while (1) {
        if (dev->count <= transfer_length) {
            dma_log("%sing %i bytes to %08X\n", sop, dev->count, dev->addr);
            if (out)
                dma_bm_read(dev->addr, (uint8_t *) (data + buffer_pos), dev->count, 4);
            else
                dma_bm_write(dev->addr, (uint8_t *) (data + buffer_pos), dev->count, 4);
            transfer_length -= dev->count;
            buffer_pos += dev->count;
        } else {
            dma_log("%sing %i bytes to %08X\n", sop, transfer_length, dev->addr);
            if (out)
                dma_bm_read(dev->addr, (uint8_t *) (data + buffer_pos), transfer_length, 4);
            else
                dma_bm_write(dev->addr, (uint8_t *) (data + buffer_pos), transfer_length, 4);
            /* Increase addr and decrease count so that resumed transfers do not mess up. */
            dev->addr += transfer_length;
            dev->count -= transfer_length;
            transfer_length = 0;
            force_end       = 1;
        }

        if (force_end) {
            dma_log("Total transfer length smaller than sum of all blocks, partial block\n");
            return 1; /* This block has exhausted the data to transfer and it was smaller than the count, break. */
        } else {
            if (!transfer_length && !dev->eot) {
                dma_log("Total transfer length smaller than sum of all blocks, full block\n");
                return 1; /* We have exhausted the data to transfer but there's more blocks left, break. */
            } else if (transfer_length && dev->eot) {
                dma_log("Total transfer length greater than sum of all blocks\n");
                return 4; /* There is data left to transfer but we have reached EOT - return with error. */
            } else if (dev->eot) {
                dma_log("Regular EOT\n");
                return 5; /* We have regularly reached EOT - clear status and break. */
            } else {
                /* We have more to transfer and there are blocks left, get next block. */
                dma_sg_next_addr(dev);
            }
        }
    }

    return 1;
}

uint8_t
_dma_read(uint32_t addr, dma_t *dma_c)
{
    uint8_t temp;

    if (dma_advanced) {
        if (dma_c->sg_status & 1)
            dma_c->sg_status = (dma_c->sg_status & 0x0f) | (dma_sg(&temp, 1, 1, dma_c) << 4);
        else
            dma_bm_read(addr, &temp, 1, dma_transfer_size(dma_c));
    } else
        temp = mem_readb_phys(addr);

    return (temp);
}

static uint16_t
_dma_readw(uint32_t addr, dma_t *dma_c)
{
    uint16_t temp;

    if (dma_advanced) {
        if (dma_c->sg_status & 1)
            dma_c->sg_status = (dma_c->sg_status & 0x0f) | (dma_sg((uint8_t *) &temp, 2, 1, dma_c) << 4);
        else
            dma_bm_read(addr, (uint8_t *) &temp, 2, dma_transfer_size(dma_c));
    } else
        temp = _dma_read(addr, dma_c) | (_dma_read(addr + 1, dma_c) << 8);

    return (temp);
}

static void
_dma_write(uint32_t addr, uint8_t val, dma_t *dma_c)
{
    if (dma_advanced) {
        if (dma_c->sg_status & 1)
            dma_c->sg_status = (dma_c->sg_status & 0x0f) | (dma_sg(&val, 1, 0, dma_c) << 4);
        else
            dma_bm_write(addr, &val, 1, dma_transfer_size(dma_c));
    } else {
        mem_writeb_phys(addr, val);
        if (dma_at)
            mem_invalidate_range(addr, addr);
    }
}

static void
_dma_writew(uint32_t addr, uint16_t val, dma_t *dma_c)
{
    if (dma_advanced) {
        if (dma_c->sg_status & 1)
            dma_c->sg_status = (dma_c->sg_status & 0x0f) | (dma_sg((uint8_t *) &val, 2, 0, dma_c) << 4);
        else
            dma_bm_write(addr, (uint8_t *) &val, 2, dma_transfer_size(dma_c));
    } else {
        _dma_write(addr, val & 0xff, dma_c);
        _dma_write(addr + 1, val >> 8, dma_c);
    }
}

static void
dma_retreat(dma_t *dma_c)
{
    int as = dma_c->transfer_mode >> 8;

    if (dma->sg_status & 1) {
        dma_c->ac = (dma_c->ac - as) & dma_mask;

        dma_c->page = dma_c->page_l = (dma_c->ac >> 16) & 0xff;
        dma_c->page_h               = (dma_c->ac >> 24) & 0xff;
    } else if (as == 2)
        dma_c->ac = ((dma_c->ac & 0xfffe0000) & dma_mask) | ((dma_c->ac - as) & 0xffff);
    else
        dma_c->ac = ((dma_c->ac & 0xffff0000) & dma_mask) | ((dma_c->ac - as) & 0xffff);
}

void
dma_advance(dma_t *dma_c)
{
    int as = dma_c->transfer_mode >> 8;

    if (dma->sg_status & 1) {
        dma_c->ac = (dma_c->ac + as) & dma_mask;

        dma_c->page = dma_c->page_l = (dma_c->ac >> 16) & 0xff;
        dma_c->page_h               = (dma_c->ac >> 24) & 0xff;
    } else if (as == 2)
        dma_c->ac = ((dma_c->ac & 0xfffe0000) & dma_mask) | ((dma_c->ac + as) & 0xffff);
    else
        dma_c->ac = ((dma_c->ac & 0xffff0000) & dma_mask) | ((dma_c->ac + as) & 0xffff);
}

int
dma_channel_read(int channel)
{
    dma_t   *dma_c = &dma[channel];
    uint16_t temp;
    int      tc = 0;

    if (channel < 4) {
        if (dma_command[0] & 0x04)
            return (DMA_NODATA);
    } else {
        if (dma_command[1] & 0x04)
            return (DMA_NODATA);
    }

    if (!(dma_e & (1 << channel)))
        return (DMA_NODATA);
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        return (DMA_NODATA);
    if ((dma_c->mode & 0xC) != 8)
        return (DMA_NODATA);

    if (!dma_at && !channel)
        refreshread();

    if (!dma_c->size) {
        temp = _dma_read(dma_c->ac, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac--;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac - 1) & 0xffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac++;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac + 1) & 0xffff);
        }
    } else {
        temp = _dma_readw(dma_c->ac, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac -= 2;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac - 2) & 0x1ffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac += 2;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac + 2) & 0x1ffff);
        }
    }

    dma_stat_rq |= (1 << channel);

    dma_c->cc--;
    if (dma_c->cc < 0) {
        if (dma_advanced && (dma_c->sg_status & 1) && !(dma_c->sg_status & 6))
            dma_sg_next_addr(dma_c);
        else {
            tc = 1;
            if (dma_c->mode & 0x10) { /*Auto-init*/
                dma_c->cc = dma_c->cb;
                dma_c->ac = dma_c->ab;
            } else
                dma_m |= (1 << channel);
            dma_stat |= (1 << channel);
        }
    }

    if (tc) {
        if (dma_advanced && (dma_c->sg_status & 1) && ((dma_c->sg_command & 0xc0) == 0x40)) {
            picint(1 << 13);
            dma_c->sg_status |= 8;
        }

        return (temp | DMA_OVER);
    }

    return (temp);
}

int
dma_channel_write(int channel, uint16_t val)
{
    dma_t *dma_c = &dma[channel];

    if (channel < 4) {
        if (dma_command[0] & 0x04)
            return (DMA_NODATA);
    } else {
        if (dma_command[1] & 0x04)
            return (DMA_NODATA);
    }

    if (!(dma_e & (1 << channel)))
        return (DMA_NODATA);
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        return (DMA_NODATA);
    if ((dma_c->mode & 0xC) != 4)
        return (DMA_NODATA);

    if (!dma_c->size) {
        _dma_write(dma_c->ac, val & 0xff, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac--;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac - 1) & 0xffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac++;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac + 1) & 0xffff);
        }
    } else {
        _dma_writew(dma_c->ac, val, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac -= 2;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac - 2) & 0x1ffff);
            dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac - 2) & 0x1ffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac += 2;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac + 2) & 0x1ffff);
        }
    }

    dma_stat_rq |= (1 << channel);

    dma_c->cc--;
    if (dma_c->cc < 0) {
        if (dma_advanced && (dma_c->sg_status & 1) && !(dma_c->sg_status & 6))
            dma_sg_next_addr(dma_c);
        else {
            if (dma_c->mode & 0x10) { /*Auto-init*/
                dma_c->cc = dma_c->cb;
                dma_c->ac = dma_c->ab;
            } else
                dma_m |= (1 << channel);
            dma_stat |= (1 << channel);
        }
    }

    if (dma_m & (1 << channel)) {
        if (dma_advanced && (dma_c->sg_status & 1) && ((dma_c->sg_command & 0xc0) == 0x40)) {
            picint(1 << 13);
            dma_c->sg_status |= 8;
        }

        return (DMA_OVER);
    }

    return (0);
}

static void
dma_ps2_run(int channel)
{
    dma_t *dma_c = &dma[channel];

    switch (dma_c->ps2_mode & DMA_PS2_XFER_MASK) {
        case DMA_PS2_XFER_MEM_TO_IO:
            do {
                if (!dma_c->size) {
                    uint8_t temp = _dma_read(dma_c->ac, dma_c);

                    outb(dma_c->io_addr, temp);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac--;
                    else
                        dma_c->ac++;
                } else {
                    uint16_t temp = _dma_readw(dma_c->ac, dma_c);

                    outw(dma_c->io_addr, temp);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac -= 2;
                    else
                        dma_c->ac += 2;
                }

                dma_stat_rq |= (1 << channel);
                dma_c->cc--;
            } while (dma_c->cc > 0);

            dma_stat |= (1 << channel);
            break;

        case DMA_PS2_XFER_IO_TO_MEM:
            do {
                if (!dma_c->size) {
                    uint8_t temp = inb(dma_c->io_addr);

                    _dma_write(dma_c->ac, temp, dma_c);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac--;
                    else
                        dma_c->ac++;
                } else {
                    uint16_t temp = inw(dma_c->io_addr);

                    _dma_writew(dma_c->ac, temp, dma_c);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac -= 2;
                    else
                        dma_c->ac += 2;
                }

                dma_stat_rq |= (1 << channel);
                dma_c->cc--;
            } while (dma_c->cc > 0);

            ps2_cache_clean();
            dma_stat |= (1 << channel);
            break;

        default: /*Memory verify*/
            do {
                if (!dma_c->size) {
                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac--;
                    else
                        dma_c->ac++;
                } else {
                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac -= 2;
                    else
                        dma_c->ac += 2;
                }

                dma_stat_rq |= (1 << channel);
                dma->cc--;
            } while (dma->cc > 0);

            dma_stat |= (1 << channel);
            break;
    }
}

int
dma_mode(int channel)
{
    return (dma[channel].mode);
}

/* DMA Bus Master Page Read/Write */
void
dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize)
{
    uint32_t i        = 0, n, n2;
    uint8_t  bytes[4] = { 0, 0, 0, 0 };

    n  = TotalSize & ~(TransferSize - 1);
    n2 = TotalSize - n;

    /* Do the divisible block, if there is one. */
    if (n) {
        for (i = 0; i < n; i += TransferSize)
            mem_read_phys((void *) &(DataRead[i]), PhysAddress + i, TransferSize);
    }

    /* Do the non-divisible block, if there is one. */
    if (n2) {
        mem_read_phys((void *) bytes, PhysAddress + n, TransferSize);
        memcpy((void *) &(DataRead[n]), bytes, n2);
    }
}

void
dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize)
{
    uint32_t i        = 0, n, n2;
    uint8_t  bytes[4] = { 0, 0, 0, 0 };

    n  = TotalSize & ~(TransferSize - 1);
    n2 = TotalSize - n;

    /* Do the divisible block, if there is one. */
    if (n) {
        for (i = 0; i < n; i += TransferSize)
            mem_write_phys((void *) &(DataWrite[i]), PhysAddress + i, TransferSize);
    }

    /* Do the non-divisible block, if there is one. */
    if (n2) {
        mem_read_phys((void *) bytes, PhysAddress + n, TransferSize);
        memcpy(bytes, (void *) &(DataWrite[n]), n2);
        mem_write_phys((void *) bytes, PhysAddress + n, TransferSize);
    }

    if (dma_at)
        mem_invalidate_range(PhysAddress, PhysAddress + TotalSize - 1);
}
