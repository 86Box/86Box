/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 * Adaptec AIC-6360: AHA-1520A and Sound Blaster 16 SCSI-2 interface.
 *
 * Register behavior follows the Adaptec AIC-6360 Data Book. These boards
 * use programmed I/O; the SB16 interface has neither DMA nor a boot ROM.
 * Targets remain connected and negotiate asynchronous, narrow transfers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_aic6360.h>

#define AIC6360_ROM "roms/scsi/adaptec/aic6360-v1-20l.bin"

static const uint8_t aic6360_cdb_length[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };

enum {
    SCSISEQ,
    SXFRCTL0,
    SXFRCTL1,
    SCSISIG,
    SCSIRATE,
    SCSIID,
    SCSIDAT,
    SCSIBUS,
    STCNT0,
    STCNT1,
    STCNT2,
    SSTAT0,
    SSTAT1,
    SSTAT2,
    SSTAT3,
    SSTAT4,
    SIMODE0,
    SIMODE1,
    DMACNTRL0,
    DMACNTRL1,
    DMASTAT,
    FIFOSTAT,
    DATAPORT,
    BRSTCNTRL = 0x18,
    PORTA     = 0x1a,
    PORTB,
    REV,
    STACK,
    TEST
};

enum {
    AIC_DATA_OUT = 0x00,
    AIC_DATA_IN  = 0x40,
    AIC_COMMAND  = 0x80,
    AIC_STATUS   = 0xc0,
    AIC_MSG_OUT  = 0xa0,
    AIC_MSG_IN   = 0xe0,
    AIC_BUS_FREE = 0xff
};

#define ENSELO   0x40
#define SCSIRSTO 0x01
#define SPIOEN   0x08
#define SELDO    0x40
#define SELINGO  0x10
#define SWRAP    0x08
#define SPIORDY  0x02
#define SELTO    0x80
#define SCSIRSTI 0x20
#define PHASEMIS 0x10
#define BUSFREE  0x08
#define PHASECHG 0x02
#define REQINIT  0x01
#define ENDMA    0x80
#define DWORDPIO 0x10
#define WRITE    0x08
#define INTEN    0x04
#define RSTFIFO  0x02
#define SWINT    0x01

/* The byte FIFO deliberately retains residual data across a phase change. */
typedef struct aic6360_t {
    uint8_t    regs[32];
    uint8_t    stack[32];
    uint8_t    stack_pos;
    uint8_t    fifo[128];
    unsigned   fifo_pos, fifo_count;
    uint8_t    bus, irq, irq_state, seldo_ack;
    uint16_t   base;
    uint8_t    phase, target, lun, atn, req;
    uint8_t    selecting, command_pending, command_active;
    uint8_t    pio_pending;
    uint8_t    cdb[16], cdb_pos;
    uint8_t    msg[8], reply[5], reply_pos, reply_len, msg_complete;
    unsigned   msg_pos, msg_len;
    uint32_t   data_pos, transfer_count;
    pc_timer_t timer;
    rom_t      bios;
} aic6360_t;

static void aic6360_service(aic6360_t *dev);

static scsi_device_t *
aic6360_target(aic6360_t *dev)
{
    return &scsi_devices[dev->bus][dev->target];
}

static uint8_t
aic6360_status0(const aic6360_t *dev)
{
    return dev->regs[SSTAT0] | ((dev->req && (dev->regs[SXFRCTL0] & SPIOEN)) ? SPIORDY : 0);
}

static uint8_t
aic6360_status1(const aic6360_t *dev)
{
    uint8_t status = dev->regs[SSTAT1];
    if (dev->req && (status & REQINIT) && dev->phase != (dev->regs[SCSISIG] & 0xe0))
        status |= PHASEMIS;
    return status;
}

static int
aic6360_interrupt(const aic6360_t *dev)
{
    return ((aic6360_status0(dev) & ~dev->seldo_ack & dev->regs[SIMODE0]) || (aic6360_status1(dev) & dev->regs[SIMODE1]) || (dev->regs[DMACNTRL0] & SWINT));
}

static void
aic6360_irq(aic6360_t *dev)
{
    int asserted = aic6360_interrupt(dev) && (dev->regs[DMACNTRL0] & INTEN);
    if (asserted != dev->irq_state) {
        dev->irq_state = asserted;
        if (asserted)
            picint(1 << dev->irq);
        else
            picintc(1 << dev->irq);
    }
}

static void
aic6360_request(aic6360_t *dev)
{
    dev->req = 1;
    dev->regs[SSTAT1] |= REQINIT;
    if (dev->phase != (dev->regs[SCSISIG] & 0xe0))
        dev->regs[SSTAT1] |= PHASECHG;
}

static void
aic6360_phase(aic6360_t *dev, uint8_t phase)
{
    dev->phase = phase;
    dev->req   = 0;
    dev->regs[SSTAT1] &= ~REQINIT;
    if (phase == AIC_BUS_FREE) {
        dev->regs[SSTAT0] &= ~(SELDO | SELINGO);
        dev->regs[SSTAT1] |= BUSFREE;
        dev->regs[SCSISIG] = 0;
        dev->atn           = 0;
        dev->seldo_ack     = 0;
        scsi_device_identify(aic6360_target(dev), SCSI_LUN_USE_CDB);
    } else
        aic6360_request(dev);
}

static void
aic6360_abort(aic6360_t *dev)
{
    timer_stop(&dev->timer);
    if (dev->command_active)
        scsi_device_command_stop(aic6360_target(dev));
    dev->command_active = dev->command_pending = dev->selecting = 0;
    dev->pio_pending                                            = 0;
    dev->fifo_count = dev->fifo_pos = 0;
    aic6360_phase(dev, AIC_BUS_FREE);
}

static void
aic6360_bus_reset(aic6360_t *dev)
{
    aic6360_abort(dev);
    for (int i = 0; i < 8; i++)
        scsi_device_reset(&scsi_devices[dev->bus][i]);
    dev->regs[SSTAT1] |= SCSIRSTI;
}

static void
aic6360_finish_data(aic6360_t *dev)
{
    scsi_device_command_phase1(aic6360_target(dev));
    dev->command_active = 0;
    aic6360_phase(dev, AIC_STATUS);
}

static void
aic6360_command_ready(aic6360_t *dev)
{
    scsi_device_t *sd    = aic6360_target(dev);
    dev->command_pending = 0;
    if (sd->status == SCSI_STATUS_OK && sd->buffer_length > 0 && sd->sc && sd->sc->temp_buffer && (sd->phase == SCSI_PHASE_DATA_IN || sd->phase == SCSI_PHASE_DATA_OUT))
        aic6360_phase(dev, sd->phase == SCSI_PHASE_DATA_IN ? AIC_DATA_IN : AIC_DATA_OUT);
    else {
        dev->command_active = 0;
        aic6360_phase(dev, AIC_STATUS);
    }
}

static void
aic6360_callback(void *priv)
{
    aic6360_t *dev = priv;
    if (dev->selecting) {
        dev->selecting = 0;
        if (dev->target != ((dev->regs[SCSIID] >> 4) & 7) && scsi_device_present(aic6360_target(dev))) {
            dev->regs[SSTAT0] = (dev->regs[SSTAT0] & ~SELINGO) | SELDO;
            dev->seldo_ack    = 0;
            dev->cdb_pos = dev->msg_pos = dev->msg_len = 0;
            dev->lun                                   = SCSI_LUN_USE_CDB;
            memset(dev->cdb, 0, sizeof(dev->cdb));
            scsi_device_identify(aic6360_target(dev), dev->lun);
            if (dev->regs[SCSISEQ] & 0x08)
                dev->atn = 1;
            aic6360_phase(dev, dev->atn ? AIC_MSG_OUT : AIC_COMMAND);
        } else {
            dev->regs[SSTAT1] |= SELTO;
            aic6360_phase(dev, AIC_BUS_FREE);
        }
    } else if (dev->command_pending)
        aic6360_command_ready(dev);
    aic6360_service(dev);
}

static void
aic6360_select(aic6360_t *dev)
{
    if (dev->phase != AIC_BUS_FREE || dev->selecting || (dev->regs[SCSISEQ] & 0x81))
        return;
    dev->target    = dev->regs[SCSIID] & 7;
    dev->selecting = 1;
    dev->regs[SSTAT0] |= SELINGO;
    if (dev->target != ((dev->regs[SCSIID] >> 4) & 7) && scsi_device_present(aic6360_target(dev)))
        timer_on_auto(&dev->timer, 10.0);
    else if (dev->regs[SXFRCTL1] & 4)
        timer_on_auto(&dev->timer, 256000.0 / (1 << ((dev->regs[SXFRCTL1] >> 3) & 3)));
}

static uint8_t
aic6360_peek(aic6360_t *dev)
{
    scsi_device_t *sd = aic6360_target(dev);
    switch (dev->phase) {
        case AIC_DATA_IN:
            return sd->sc->temp_buffer[dev->data_pos];
        case AIC_STATUS:
            return sd->status;
        case AIC_MSG_IN:
            return dev->reply[dev->reply_pos];
        default:
            return dev->regs[SCSIDAT];
    }
}

static void
aic6360_message(aic6360_t *dev, uint8_t val)
{
    if (dev->msg_pos < sizeof(dev->msg))
        dev->msg[dev->msg_pos] = val;
    dev->msg_pos++;
    if (dev->msg_pos == 1)
        dev->msg_len = val == 1 ? 2 : ((val >= 0x20 && val <= 0x2f) ? 2 : 1);
    else if (dev->msg_pos == 2 && dev->msg[0] == 1)
        dev->msg_len = (unsigned) val + 2;
    if (dev->msg_pos < dev->msg_len)
        return;

    dev->reply_pos = dev->reply_len = dev->msg_complete = 0;
    if (dev->msg[0] & 0x80) {
        dev->lun = dev->msg[0] & 7;
        scsi_device_identify(aic6360_target(dev), dev->lun);
    } else if (dev->msg[0] == 0x06 || dev->msg[0] == 0x0c) {
        if (dev->msg[0] == 0x0c)
            scsi_device_reset(aic6360_target(dev));
        aic6360_abort(dev);
        return;
    } else if (dev->msg[0] == 1 && dev->msg_len == 5 && dev->msg[2] == 1) {
        /* SDTR: a zero offset selects asynchronous transfers. */
        uint8_t reply[] = { 1, 3, 1, dev->msg[3], 0 };
        memcpy(dev->reply, reply, sizeof(reply));
        dev->reply_len = sizeof(reply);
    } else if (dev->msg[0] == 1 && dev->msg_len == 4 && dev->msg[2] == 3) {
        uint8_t reply[] = { 1, 2, 3, 0 }; /* WDTR: eight-bit bus. */
        memcpy(dev->reply, reply, sizeof(reply));
        dev->reply_len = sizeof(reply);
    } else if (dev->msg[0] != 0x08 && dev->msg[0] != 0x07) {
        dev->reply[0]  = 0x07; /* MESSAGE REJECT. */
        dev->reply_len = 1;
    }
    dev->msg_pos = dev->msg_len = 0;
    if (dev->reply_len)
        aic6360_phase(dev, AIC_MSG_IN);
    else if (!dev->atn)
        aic6360_phase(dev, AIC_COMMAND);
}

/* Complete one SCSI REQ/ACK handshake. */
static void
aic6360_transfer(aic6360_t *dev, uint8_t val)
{
    scsi_device_t *sd = aic6360_target(dev);
    if (!dev->req)
        return;
    dev->req = 0;
    dev->regs[SSTAT1] &= ~REQINIT;
    switch (dev->phase) {
        case AIC_MSG_OUT:
            aic6360_message(dev, val);
            break;
        case AIC_COMMAND:
            dev->cdb[dev->cdb_pos++] = val;
            if (dev->cdb_pos == aic6360_cdb_length[dev->cdb[0] >> 5]) {
                sd->buffer_length = -1;
                scsi_device_command_phase0(sd, dev->cdb);
                dev->data_pos       = 0;
                dev->command_active = dev->command_pending = 1;
                double delay                               = scsi_device_get_callback(sd);
                timer_on_auto(&dev->timer, delay > 0.0 ? delay : 10.0);
            }
            break;
        case AIC_DATA_OUT:
            sd->sc->temp_buffer[dev->data_pos++] = val;
            if (dev->data_pos == (uint32_t) sd->buffer_length)
                aic6360_finish_data(dev);
            break;
        case AIC_DATA_IN:
            if (++dev->data_pos == (uint32_t) sd->buffer_length)
                aic6360_finish_data(dev);
            break;
        case AIC_STATUS:
            dev->reply[0]  = MSG_COMMAND_COMPLETE;
            dev->reply_pos = 0;
            dev->reply_len = dev->msg_complete = 1;
            aic6360_phase(dev, AIC_MSG_IN);
            break;
        case AIC_MSG_IN:
            if (++dev->reply_pos == dev->reply_len)
                aic6360_phase(dev, dev->msg_complete ? AIC_BUS_FREE : (dev->atn ? AIC_MSG_OUT : AIC_COMMAND));
            break;
        default:
            break;
    }
    if (dev->phase != AIC_BUS_FREE && !dev->command_pending)
        aic6360_request(dev);
}

static void
aic6360_count(aic6360_t *dev)
{
    dev->transfer_count = (dev->transfer_count + 1) & 0xffffff;
    if (!dev->transfer_count)
        dev->regs[SSTAT0] |= SWRAP;
}

static void
aic6360_service(aic6360_t *dev)
{
    /* The data latch may be loaded before SPIOEN is asserted. The BIOS
       uses this to send its final message byte after dropping ATN. */
    if (dev->pio_pending && dev->req && !(dev->phase & 0x40) && (dev->regs[SXFRCTL0] & SPIOEN)) {
        dev->pio_pending = 0;
        aic6360_transfer(dev, dev->regs[SCSIDAT]);
    }
    /* PIO accesses clock the two FIFO interfaces. A phase mismatch stops
       SCSI transfers but never discards bytes already in the host FIFO. */
    if (!(dev->regs[DMACNTRL1] & 0x80) && (dev->regs[SXFRCTL0] & 0xe8) == 0xe0) {
        while (dev->req && dev->phase == (dev->regs[SCSISIG] & 0xe0)) {
            if (dev->regs[DMACNTRL0] & WRITE) {
                if ((dev->phase & 0x40) || !dev->fifo_count)
                    break;
                uint8_t val   = dev->fifo[dev->fifo_pos];
                dev->fifo_pos = (dev->fifo_pos + 1) & 127;
                dev->fifo_count--;
                aic6360_transfer(dev, val);
            } else {
                if (!(dev->phase & 0x40) || dev->fifo_count == 128)
                    break;
                dev->fifo[(dev->fifo_pos + dev->fifo_count++) & 127] = aic6360_peek(dev);
                aic6360_transfer(dev, 0);
            }
            aic6360_count(dev);
        }
    }
    aic6360_irq(dev);
}

static int
aic6360_data_port(const aic6360_t *dev, unsigned reg)
{
    return (reg == DATAPORT || reg == DATAPORT + 1 || ((dev->regs[DMACNTRL0] & (DWORDPIO | ENDMA)) == (DWORDPIO | ENDMA) && reg >= 0x18 && reg <= 0x1b));
}

static uint8_t
aic6360_read(uint16_t port, void *priv)
{
    aic6360_t *dev = priv;
    unsigned   reg = port & 0x1f;
    uint8_t    ret = 0;
    aic6360_service(dev);
    if (aic6360_data_port(dev, reg)) {
        if ((dev->regs[DMACNTRL0] & (ENDMA | WRITE)) == ENDMA && dev->fifo_count) {
            ret           = dev->fifo[dev->fifo_pos];
            dev->fifo_pos = (dev->fifo_pos + 1) & 127;
            dev->fifo_count--;
        }
    } else
        switch (reg) {
            case SCSISIG:
                ret = (dev->phase == AIC_BUS_FREE ? 0 : dev->phase | 4 | (dev->req ? 2 : 0)) | (dev->atn ? 0x10 : 0) | (dev->regs[SCSISIG] & 0x0d);
                if (dev->selecting)
                    ret |= 8;
                break;
            case SCSIID:
                ret = (1 << ((dev->regs[SCSIID] >> 4) & 7)) | (1 << dev->target);
                break;
            case SCSIBUS:
            case SCSIDAT:
                ret = aic6360_peek(dev);
                /* Windows NT 4.0 drivers drain the SCSI bus completely on init, and expect SCSIBUS reads to return 0 if drained. */
                if (dev->phase != AIC_DATA_IN && dev->phase != AIC_STATUS && dev->phase != AIC_MSG_IN && reg == SCSIBUS)
                    ret = 0x00;
                if (reg == SCSIDAT && (dev->regs[SXFRCTL0] & SPIOEN) && (dev->phase & 0x40))
                    aic6360_transfer(dev, 0);
                break;
            case STCNT0:
            case STCNT1:
            case STCNT2:
                ret = dev->transfer_count >> (8 * (reg - STCNT0));
                break;
            case SSTAT0:
                ret = aic6360_status0(dev);
                break;
            case SSTAT1:
                ret = aic6360_status1(dev);
                break;
            case SSTAT2:
                ret = 0x10;
                break; /* SCSI FIFO empty. */
            case SSTAT3:
            case SSTAT4:
            case TEST:
                ret = 0;
                break;
            case DMASTAT:
                {
                    unsigned available = (dev->regs[DMACNTRL0] & WRITE) ? 128 - dev->fifo_count : dev->fifo_count;
                    ret                = (dev->fifo_count == 0 ? 8 : 0) | (dev->fifo_count == 128 ? 0x10 : 0) | (dev->fifo_count >= 64 ? 4 : 0) | (available >= 2 ? 0x40 : 0) | ((available >= 4 && (dev->regs[DMACNTRL0] & DWORDPIO)) ? 2 : 0) | (aic6360_interrupt(dev) ? 0x20 : 0);
                    break;
                }
            case FIFOSTAT:
                ret = dev->fifo_count;
                break;
            case REV:
                ret = 1;
                break;
            case STACK:
                ret            = dev->stack[dev->stack_pos];
                dev->stack_pos = (dev->stack_pos + 1) & ((dev->regs[DMACNTRL1] & 0x40) ? 31 : 15);
                break;
            default:
                ret = dev->regs[reg];
                break;
        }
    aic6360_service(dev);
    return ret;
}

static void
aic6360_write(uint16_t port, uint8_t val, void *priv)
{
    aic6360_t *dev = priv;
    unsigned   reg = port & 0x1f;
    if (aic6360_data_port(dev, reg)) {
        if ((dev->regs[DMACNTRL0] & (ENDMA | WRITE)) == (ENDMA | WRITE) && dev->fifo_count < 128)
            dev->fifo[(dev->fifo_pos + dev->fifo_count++) & 127] = val;
    } else
        switch (reg) {
            case SCSISEQ:
                {
                    uint8_t old    = dev->regs[reg];
                    dev->regs[reg] = val;
                    if ((val & SCSIRSTO) && !(old & SCSIRSTO))
                        aic6360_bus_reset(dev);
                    if (!(val & ENSELO) && dev->selecting) {
                        timer_stop(&dev->timer);
                        dev->selecting = 0;
                        dev->regs[SSTAT0] &= ~SELINGO;
                    }
                    if ((val & ENSELO) && !(old & ENSELO))
                        aic6360_select(dev);
                    break;
                }
            case SXFRCTL0:
                dev->regs[reg] = val & 0xe8;
                if (val & 0x10)
                    dev->transfer_count = 0;
                break;
            case SXFRCTL1:
                dev->regs[reg] = val & 0xfe;
                if (dev->selecting && (val & 4) && !timer_is_enabled(&dev->timer))
                    timer_on_auto(&dev->timer, 256000.0 / (1 << ((val >> 3) & 3)));
                break;
            case SCSISIG:
                {
                    uint8_t old    = dev->regs[reg];
                    dev->regs[reg] = val;
                    if (val & 0x10)
                        dev->atn = 1;
                    if (!(old & 1) && (val & 1) && !(dev->regs[SXFRCTL0] & SPIOEN)) {
                        dev->regs[SCSIDAT] = (dev->phase & 0x40) ? aic6360_peek(dev) : dev->regs[SCSIDAT];
                        dev->pio_pending   = 0;
                        aic6360_transfer(dev, dev->regs[SCSIDAT]);
                    }
                    if (dev->req && dev->phase != (val & 0xe0))
                        dev->regs[SSTAT1] |= PHASECHG;
                    break;
                }
            case SCSIID:
                dev->regs[reg] = val & 0x77;
                break;
            case SCSIDAT:
                dev->regs[reg]   = val;
                dev->pio_pending = 1;
                break;
            case STCNT0:
            case STCNT1:
            case STCNT2:
                {
                    unsigned shift      = 8 * (reg - STCNT0);
                    dev->transfer_count = (dev->transfer_count & ~(0xffu << shift)) | ((uint32_t) val << shift);
                    break;
                }
            case SSTAT0:
                /* CLRSELDO acknowledges the interrupt; SELDO stays set until bus free. */
                dev->seldo_ack |= val & SELDO;
                dev->regs[SSTAT0] &= ~(val & 0x1d);
                if (val & 0x80)
                    dev->regs[SSTAT0] |= 4;
                break;
            case SSTAT1:
                dev->regs[reg] &= ~(val & 0xaf);
                if (val & 0x40)
                    dev->atn = 0;
                break;
            case SSTAT2:
            case SSTAT3:
            case SSTAT4:
            case SCSIBUS:
            case DMASTAT:
            case FIFOSTAT:
            case REV:
                break;
            case SIMODE0:
                dev->regs[reg] = val & 0x7f;
                break;
            case SIMODE1:
                dev->regs[reg] = val;
                break;
            case DMACNTRL0:
                dev->regs[reg] = val & ~RSTFIFO;
                if (val & RSTFIFO)
                    dev->fifo_count = dev->fifo_pos = 0;
                break;
            case DMACNTRL1:
                dev->regs[reg] = val & 0xc0;
                dev->stack_pos = val & ((val & 0x40) ? 31 : 15);
                break;
            case STACK:
                dev->stack[dev->stack_pos] = val;
                dev->stack_pos             = (dev->stack_pos + 1) & ((dev->regs[DMACNTRL1] & 0x40) ? 31 : 15);
                break;
            case PORTA:
            case PORTB:
                break; /* Board configuration jumpers are read-only. */
            default:
                dev->regs[reg] = val;
                break;
        }
    aic6360_service(dev);
}

static uint16_t
aic6360_readw(uint16_t port, void *priv)
{
    uint16_t val = aic6360_read(port, priv);
    return val | ((uint16_t) aic6360_read(port + 1, priv) << 8);
}

static uint32_t
aic6360_readl(uint16_t port, void *priv)
{
    uint32_t val = aic6360_readw(port, priv);
    return val | ((uint32_t) aic6360_readw(port + 2, priv) << 16);
}

static void
aic6360_writew(uint16_t port, uint16_t val, void *priv)
{
    aic6360_write(port, val, priv);
    aic6360_write(port + 1, val >> 8, priv);
}

static void
aic6360_writel(uint16_t port, uint32_t val, void *priv)
{
    aic6360_writew(port, val, priv);
    aic6360_writew(port + 2, val >> 16, priv);
}

static void
aic6360_reset(void *priv)
{
    aic6360_t *dev = priv;
    aic6360_bus_reset(dev);
    uint8_t porta = dev->regs[PORTA], portb = dev->regs[PORTB];
    memset(dev->regs, 0, sizeof(dev->regs));
    memset(dev->stack, 0, sizeof(dev->stack));
    dev->regs[PORTA]    = porta;
    dev->regs[PORTB]    = portb;
    dev->regs[SSTAT1]   = BUSFREE;
    dev->transfer_count = dev->stack_pos = dev->cdb_pos = 0;
    dev->msg_pos = dev->msg_len = dev->reply_pos = dev->reply_len = 0;
    aic6360_irq(dev);
}

static void *
aic6360_init(const device_t *info)
{
    uint8_t bus = scsi_get_bus();
    if (bus == 0xff)
        return NULL;
    aic6360_t *dev = calloc(1, sizeof(*dev));
    dev->bus       = bus;
    dev->base      = device_get_config_hex16("base");
    dev->irq       = device_get_config_int("irq");
    dev->phase     = AIC_BUS_FREE;
    timer_add(&dev->timer, aic6360_callback, dev, 0);
    unsigned bios_addr = info->local ? device_get_config_hex20("bios_addr") : 0;
    if (bios_addr)
        rom_init(&dev->bios, AIC6360_ROM, bios_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
    /* AHA-152x jumper encoding: parity, no DMA, IRQ, host ID 7;
       disconnect allowed, asynchronous startup, all message classes. */
    dev->regs[PORTA] = 7 | ((dev->irq - 9) << 3);
    dev->regs[PORTB] = 0x14 | (bios_addr ? 0x40 : 0);
    aic6360_reset(dev);
    io_sethandler(dev->base, 0x20, aic6360_read, aic6360_readw, aic6360_readl,
                  aic6360_write, aic6360_writew, aic6360_writel, dev);
    scsi_bus_set_speed(dev->bus, 5000000.0);
    return dev;
}

static void
aic6360_close(void *priv)
{
    aic6360_t *dev = priv;
    timer_stop(&dev->timer);
    if (dev->irq_state)
        picintc(1 << dev->irq);
    io_removehandler(dev->base, 0x20, aic6360_read, aic6360_readw, aic6360_readl,
                     aic6360_write, aic6360_writew, aic6360_writel, dev);
    if (dev->bios.rom)
        mem_mapping_disable(&dev->bios.mapping);
    free(dev->bios.rom);
    free(dev);
}

static int
aha1520a_available(void)
{
    return rom_present(AIC6360_ROM);
}

// clang-format off
static const device_config_t aha1520a_config[] = {
    {
        .name = "bios_addr", .description = "BIOS address", .type = CONFIG_HEX20, .default_int = 0xdc000,
        .selection = {
            { .description = "Disabled", .value = 0 },
            { .description = "C800H", .value = 0xc8000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
            { .description = "D800H", .value = 0xd8000 },
            { .description = "DC00H", .value = 0xdc000 },
            { .description = "E000H", .value = 0xe0000 },
            { .description = "E400H", .value = 0xe4000 },
            { .description = "" }
        }
    },
    {
        .name = "base", .description = "Address", .type = CONFIG_HEX16, .default_int = 0x340,
        .selection = {
            { .description = "140H", .value = 0x140 },
            { .description = "340H", .value = 0x340 },
            { .description = "" }
        }
    },
    {
        .name = "irq", .description = "IRQ", .type = CONFIG_SELECTION, .default_int = 11,
        .selection = {
            { .description = "IRQ 9", .value = 9 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 12", .value = 12 },
            { .description = "" }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t aha1520a_device = {
    .name          = "Adaptec AHA-1520A",
    .internal_name = "aha1520a",
    .flags         = DEVICE_ISA16,
    .local         = 1,
    .init          = aic6360_init,
    .close         = aic6360_close,
    .reset         = aic6360_reset,
    .available     = aha1520a_available,
    .config        = aha1520a_config,
    .short_name    = "AHA-1520A"
};

const device_t sb16_scsi_device = {
    .name          = "Sound Blaster 16 SCSI-2 (AIC-6360)",
    .internal_name = "sb16_scsi",
    .flags         = DEVICE_ISA16,
    .init          = aic6360_init,
    .close         = aic6360_close,
    .reset         = aic6360_reset,
    .config        = &aha1520a_config[1],
    .short_name    = "SB16 SCSI"
};
