/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the NCR 5380 chip made by NCR
 *          and used in various controllers.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2019 Sarah Walker.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2017-2024 TheCollector1995.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_ncr5380.h>

int ncr5380_cmd_len[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };

#ifdef ENABLE_NCR5380_LOG
int ncr5380_do_log = ENABLE_NCR5380_LOG;

static void
ncr5380_log(const char *fmt, ...)
{
    va_list ap;

    if (ncr5380_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ncr5380_log(fmt, ...)
#endif

#define SET_BUS_STATE(ncr, state) ncr->cur_bus = (ncr->cur_bus & ~(SCSI_PHASE_MESSAGE_IN)) | (state & (SCSI_PHASE_MESSAGE_IN))

void
ncr5380_irq(ncr_t *ncr, int set_irq)
{
    if (set_irq) {
        ncr->irq_state = 1;
        ncr->isr |= STATUS_INT;
        if (ncr->irq != -1)
            picint(1 << ncr->irq);
    } else {
        ncr->irq_state = 0;
        ncr->isr &= ~STATUS_INT;
        if (ncr->irq != 1)
            picintc(1 << ncr->irq);
    }
}

void
ncr5380_set_irq(ncr_t *ncr, int irq)
{
    ncr->irq = irq;
}

static int
ncr5380_get_dev_id(uint8_t data)
{
    for (uint8_t c = 0; c < SCSI_ID_MAX; c++) {
        if (data & (1 << c))
            return c;
    }

    return -1;
}

static int
ncr5380_getmsglen(uint8_t *msgp, int len)
{
    uint8_t msg = msgp[0];
    if (msg == 0 || (msg >= 0x02 && msg <= 0x1f) || msg >= 0x80)
        return 1;
    if (msg >= 0x20 && msg <= 0x2f)
        return 2;
    if (len < 2)
        return 3;
    return msgp[1];
}

static void
ncr5380_reset(ncr_t *ncr)
{
    ncr->command_pos = 0;
    ncr->data_pos = 0;
    ncr->state = STATE_IDLE;
    ncr->clear_req = 0;
    ncr->cur_bus = 0;
    ncr->tx_data = 0;
    ncr->output_data = 0;
    ncr->data_wait = 0;
    ncr->mode = 0;
    ncr->tcr = 0;
    ncr->icr = 0;
    ncr->dma_mode = DMA_IDLE;
    ncr5380_log("NCR Reset\n");

    ncr->timer(ncr->priv, 0.0);

    for (int i = 0; i < 8; i++)
        scsi_device_reset(&scsi_devices[ncr->bus][i]);

    ncr5380_irq(ncr, 0);
}

uint32_t
ncr5380_get_bus_host(ncr_t *ncr)
{
    uint32_t bus_host = 0;

    if (ncr->icr & ICR_DBP)
        bus_host |= BUS_DBP;

    if (ncr->icr & ICR_SEL)
        bus_host |= BUS_SEL;

    if (ncr->tcr & TCR_IO)
        bus_host |= BUS_IO;

    if (ncr->tcr & TCR_CD)
        bus_host |= BUS_CD;

    if (ncr->tcr & TCR_MSG)
        bus_host |= BUS_MSG;

    if (ncr->tcr & TCR_REQ)
        bus_host |= BUS_REQ;

    if (ncr->icr & ICR_BSY)
        bus_host |= BUS_BSY;

    if (ncr->icr & ICR_ATN)
        bus_host |= BUS_ATN;

    if (ncr->icr & ICR_ACK)
        bus_host |= BUS_ACK;

    if (ncr->mode & MODE_ARBITRATE)
        bus_host |= BUS_ARB;

    return (bus_host | BUS_SETDATA(ncr->output_data));
}

void
ncr5380_bus_read(ncr_t *ncr)
{
    const scsi_device_t *dev;
    int                  phase;

    /*Wait processes to handle bus requests*/
    if (ncr->clear_req) {
        ncr->clear_req--;
        if (!ncr->clear_req) {
            ncr5380_log("Prelude to command data\n");
            SET_BUS_STATE(ncr, ncr->new_phase);
            ncr->cur_bus |= BUS_REQ;
        }
    }

    if (ncr->wait_data) {
        ncr->wait_data--;
        if (!ncr->wait_data) {
            dev = &scsi_devices[ncr->bus][ncr->target_id];
            SET_BUS_STATE(ncr, ncr->new_phase);
            phase = (ncr->cur_bus & SCSI_PHASE_MESSAGE_IN);

            if (phase == SCSI_PHASE_DATA_IN) {
                ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
                ncr->state   = STATE_DATAIN;
                ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP;
            } else if (phase == SCSI_PHASE_DATA_OUT) {
                if (ncr->new_phase & BUS_IDLE) {
                    ncr->state = STATE_IDLE;
                    ncr->cur_bus &= ~BUS_BSY;
                } else
                    ncr->state = STATE_DATAOUT;
            } else if (phase == SCSI_PHASE_STATUS) {
                ncr->cur_bus |= BUS_REQ;
                ncr->state   = STATE_STATUS;
                ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(dev->status) | BUS_DBP;
            } else if (phase == SCSI_PHASE_MESSAGE_IN) {
                ncr->state   = STATE_MESSAGEIN;
                ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(0) | BUS_DBP;
            } else if (phase == SCSI_PHASE_MESSAGE_OUT) {
                ncr->cur_bus |= BUS_REQ;
                ncr->state   = STATE_MESSAGEOUT;
                ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->target_id >> 5) | BUS_DBP;
            }
        }
    }

    if (ncr->wait_complete) {
        ncr->wait_complete--;
        if (!ncr->wait_complete)
            ncr->cur_bus |= BUS_REQ;
    }
}

void
ncr5380_bus_update(ncr_t *ncr, int bus)
{
    scsi_device_t *dev     = &scsi_devices[ncr->bus][ncr->target_id];
    double         p;
    uint8_t        sel_data;
    int            msglen;

    /*Start the SCSI command layer, which will also make the timings*/
    if (bus & BUS_ARB)
        ncr->state = STATE_IDLE;

    ncr5380_log("State = %i\n", ncr->state);

    switch (ncr->state) {
        case STATE_IDLE:
            ncr->clear_req = ncr->wait_data = ncr->wait_complete = 0;
            if ((bus & BUS_SEL) && !(bus & BUS_BSY)) {
                ncr5380_log("Selection phase\n");
                sel_data = BUS_GETDATA(bus);

                ncr->target_id = ncr5380_get_dev_id(sel_data);

                ncr5380_log("Select - target ID = %i\n", ncr->target_id);

                /*Once the device has been found and selected, mark it as busy*/
                if ((ncr->target_id != (uint8_t) -1) && scsi_device_present(&scsi_devices[ncr->bus][ncr->target_id])) {
                    ncr->cur_bus |= BUS_BSY;
                    ncr->state = STATE_SELECT;
                } else {
                    ncr5380_log("Device not found at ID %i, Current Bus BSY=%02x\n", ncr->target_id, ncr->cur_bus);
                    ncr->cur_bus = 0;
                }
            }
            break;
        case STATE_SELECT:
            if (!(bus & BUS_SEL)) {
                if (!(bus & BUS_ATN)) {
                    if ((ncr->target_id != (uint8_t) -1) && scsi_device_present(&scsi_devices[ncr->bus][ncr->target_id])) {
                        ncr5380_log("Device found at ID %i, Current Bus BSY=%02x\n", ncr->target_id, ncr->cur_bus);
                        ncr->state   = STATE_COMMAND;
                        ncr->cur_bus = BUS_BSY | BUS_REQ;
                        ncr5380_log("CurBus BSY|REQ=%02x\n", ncr->cur_bus);
                        ncr->command_pos = 0;
                        SET_BUS_STATE(ncr, SCSI_PHASE_COMMAND);
                    } else {
                        ncr->state   = STATE_IDLE;
                        ncr->cur_bus = 0;
                    }
                } else {
                    ncr5380_log("Set to SCSI Message Out\n");
                    ncr->new_phase  = SCSI_PHASE_MESSAGE_OUT;
                    ncr->wait_data  = 4;
                    ncr->msgout_pos = 0;
                    ncr->is_msgout  = 1;
                }
            }
            break;
        case STATE_COMMAND:
            if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
                /*Write command byte to the output data register*/
                ncr->command[ncr->command_pos++] = BUS_GETDATA(bus);
                ncr->clear_req                   = 3;
                ncr->new_phase                   = ncr->cur_bus & SCSI_PHASE_MESSAGE_IN;
                ncr->cur_bus &= ~BUS_REQ;

                ncr5380_log("Command pos=%i, output data=%02x\n", ncr->command_pos, BUS_GETDATA(bus));

                if (ncr->command_pos == ncr5380_cmd_len[(ncr->command[0] >> 5) & 7]) {
                    if (ncr->is_msgout) {
                        ncr->is_msgout = 0;
#if 0
                        ncr->command[1] = (ncr->command[1] & 0x1f) | (ncr->msglun << 5);
#endif
                    }

                    /*Reset data position to default*/
                    ncr->data_pos = 0;

                    dev = &scsi_devices[ncr->bus][ncr->target_id];

                    ncr5380_log("SCSI Command 0x%02X for ID %d, status code=%02x\n", ncr->command[0], ncr->target_id, dev->status);
                    dev->buffer_length = -1;
                    scsi_device_command_phase0(dev, ncr->command);
                    ncr5380_log("SCSI ID %i: Command %02X: Buffer Length %i, SCSI Phase %02X\n", ncr->target_id, ncr->command[0], dev->buffer_length, dev->phase);

                    ncr->period     = 1.0;
                    ncr->wait_data  = 4;
                    ncr->data_wait  = 0;

                    if (dev->status == SCSI_STATUS_OK) {
                        /*If the SCSI phase is Data In or Data Out, allocate the SCSI buffer based on the transfer length of the command*/
                        if (dev->buffer_length && ((dev->phase == SCSI_PHASE_DATA_IN) || (dev->phase == SCSI_PHASE_DATA_OUT))) {
                            p = scsi_device_get_callback(dev);
                            ncr->period = (p > 0.0) ? p : (((double) dev->buffer_length) * 0.2);
                            ncr5380_log("SCSI ID %i: command 0x%02x for p = %lf, update = %lf, len = %i, dmamode = %x\n", ncr->target_id, ncr->command[0], scsi_device_get_callback(dev), ncr->period, dev->buffer_length, ncr->dma_mode);
                        }
                    }
                    ncr->new_phase = dev->phase;
                }
            }
            break;
        case STATE_DATAIN:
            dev = &scsi_devices[ncr->bus][ncr->target_id];
            if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
                if (ncr->data_pos >= dev->buffer_length) {
                    ncr->cur_bus &= ~BUS_REQ;
                    scsi_device_command_phase1(dev);
                    ncr->new_phase     = SCSI_PHASE_STATUS;
                    ncr->wait_data     = 4;
                    ncr->wait_complete = 8;
                } else {
                    ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
                    ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP | BUS_REQ;
                    if (ncr->dma_mode == DMA_IDLE) { /*If a data in command that is not read 6/10 has been issued*/
                        ncr->data_wait |= 1;
                        ncr5380_log("DMA mode idle in\n");
                        ncr->timer(ncr->priv, ncr->period);
                    } else {
                        ncr5380_log("DMA mode IN.\n");
                        ncr->clear_req = 3;
                    }

                    ncr->cur_bus &= ~BUS_REQ;
                    ncr->new_phase = SCSI_PHASE_DATA_IN;
                }
            }
            break;
        case STATE_DATAOUT:
            dev = &scsi_devices[ncr->bus][ncr->target_id];
            if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
                dev->sc->temp_buffer[ncr->data_pos++] = BUS_GETDATA(bus);

                if (ncr->data_pos >= dev->buffer_length) {
                    ncr->cur_bus &= ~BUS_REQ;
                    scsi_device_command_phase1(dev);
                    ncr->new_phase     = SCSI_PHASE_STATUS;
                    ncr->wait_data     = 4;
                    ncr->wait_complete = 8;
                } else {
                    /*More data is to be transferred, place a request*/
                    if (ncr->dma_mode == DMA_IDLE) { /*If a data out command that is not write 6/10 has been issued*/
                        ncr->data_wait |= 1;
                        ncr5380_log("DMA mode idle out\n");
                        ncr->timer(ncr->priv, ncr->period);
                    } else
                        ncr->clear_req = 3;

                    ncr->cur_bus &= ~BUS_REQ;
                    ncr5380_log("CurBus ~REQ_DataOut=%02x\n", ncr->cur_bus);
                }
            }
            break;
        case STATE_STATUS:
            if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
                /*All transfers done, wait until next transfer*/
                scsi_device_identify(&scsi_devices[ncr->bus][ncr->target_id], SCSI_LUN_USE_CDB);
                ncr->cur_bus &= ~BUS_REQ;
                ncr->new_phase     = SCSI_PHASE_MESSAGE_IN;
                ncr->wait_data     = 4;
                ncr->wait_complete = 8;
            }
            break;
        case STATE_MESSAGEIN:
            if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
                ncr->cur_bus &= ~BUS_REQ;
                ncr->new_phase = BUS_IDLE;
                ncr->wait_data = 4;
            }
            break;
        case STATE_MESSAGEOUT:
            ncr5380_log("Ack on MSGOUT = %02x\n", (bus & BUS_ACK));
            if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
                ncr->msgout[ncr->msgout_pos++] = BUS_GETDATA(bus);
                msglen                         = ncr5380_getmsglen(ncr->msgout, ncr->msgout_pos);
                if (ncr->msgout_pos >= msglen) {
                    if ((ncr->msgout[0] & (0x80 | 0x20)) == 0x80)
                        ncr->msglun = ncr->msgout[0] & 7;
                    ncr->cur_bus &= ~BUS_REQ;
                    ncr->state = STATE_MESSAGE_ID;
                }
            }
            break;
        case STATE_MESSAGE_ID:
            if ((ncr->target_id != (uint8_t) -1) && scsi_device_present(&scsi_devices[ncr->bus][ncr->target_id])) {
                ncr5380_log("Device found at ID %i on MSGOUT, Current Bus BSY=%02x\n", ncr->target_id, ncr->cur_bus);
                scsi_device_identify(&scsi_devices[ncr->bus][ncr->target_id], ncr->msglun);
                ncr->state   = STATE_COMMAND;
                ncr->cur_bus = BUS_BSY | BUS_REQ;
                ncr5380_log("CurBus BSY|REQ=%02x\n", ncr->cur_bus);
                ncr->command_pos = 0;
                SET_BUS_STATE(ncr, SCSI_PHASE_COMMAND);
            }
            break;

        default:
            break;
    }

    ncr->bus_in = bus;
}

void
ncr5380_write(uint16_t port, uint8_t val, ncr_t *ncr)
{
    int                  bus_host = 0;

    ncr5380_log("NCR5380 write(%04x,%02x)\n", port & 7, val);

    switch (port & 7) {
        case 0: /* Output data register */
            ncr5380_log("Write: Output data register, val = %02x\n", val);
            ncr->output_data = val;
            break;

        case 1: /* Initiator Command Register */
            ncr5380_log("Write: Initiator command register\n");
            if ((val & 0x80) && !(ncr->icr & 0x80)) {
                ncr5380_log("Resetting the 5380\n");
                ncr5380_reset(ncr);
            }
            ncr->icr = val;
            break;

        case 2: /* Mode register */
            ncr5380_log("Write: Mode register, val=%02x.\n", val);
            if ((val & MODE_ARBITRATE) && !(ncr->mode & MODE_ARBITRATE)) {
                ncr->icr &= ~ICR_ARB_LOST;
                ncr->icr |= ICR_ARB_IN_PROGRESS;
            }

            ncr->mode = val;

            ncr->dma_mode_ext(ncr, ncr->priv);
            break;

        case 3: /* Target Command Register */
            ncr5380_log("Write: Target Command register\n");
            ncr->tcr = val;
            break;

        case 4: /* Select Enable Register */
            ncr5380_log("Write: Select Enable register\n");
            break;

        case 5: /* start DMA Send */
            ncr5380_log("Write: start DMA send register\n");
            /*a Write 6/10 has occurred, start the timer when the block count is loaded*/
            ncr->dma_mode = DMA_SEND;
            if (ncr->dma_send_ext)
                ncr->dma_send_ext(ncr, ncr->priv);
            break;

        case 7: /* start DMA Initiator Receive */
            ncr5380_log("Write: start DMA initiator receive register, dma? = %02x\n", ncr->mode & MODE_DMA);
            /*a Read 6/10 has occurred, start the timer when the block count is loaded*/
            ncr->dma_mode = DMA_INITIATOR_RECEIVE;
            if (ncr->dma_initiator_receive_ext)
                ncr->dma_initiator_receive_ext(ncr, ncr->priv);
            break;

        default:
            ncr5380_log("NCR5380: bad write %04x %02x\n", port, val);
            break;
    }

    bus_host = ncr5380_get_bus_host(ncr);
    ncr5380_bus_update(ncr, bus_host);
}

uint8_t
ncr5380_read(uint16_t port, ncr_t *ncr)
{
    uint8_t    ret          = 0xff;
    int        bus;
    int        bus_state;

    switch (port & 7) {
        case 0: /* Current SCSI data */
            ncr5380_log("Read: Current SCSI data register\n");
            if (ncr->icr & ICR_DBP) {
                /*Return the data from the output register if on data bus phase from ICR*/
                ncr5380_log("Data Bus Phase, ret = %02x\n", ncr->output_data);
                ret = ncr->output_data;
            } else {
                /*Return the data from the SCSI bus*/
                ncr5380_bus_read(ncr);
                ncr5380_log("NCR GetData=%02x\n", BUS_GETDATA(ncr->cur_bus));
                ret = BUS_GETDATA(ncr->cur_bus);
            }
            break;

        case 1: /* Initiator Command Register */
            ncr5380_log("Read: Initiator Command register, NCR ICR Read=%02x\n", ncr->icr);
            ret = ncr->icr;
            break;

        case 2: /* Mode register */
            ncr5380_log("Read: Mode register = %02x.\n", ncr->mode);
            ret = ncr->mode;
            break;

        case 3: /* Target Command Register */
            ncr5380_log("Read: Target Command register, NCR target stat=%02x\n", ncr->tcr);
            ret = ncr->tcr;
            break;

        case 4: /* Current SCSI Bus status */
            ncr5380_log("Read: SCSI bus status register\n");
            ret = 0;
            ncr5380_bus_read(ncr);
            ncr5380_log("NCR cur bus stat=%02x\n", ncr->cur_bus & 0xff);
            ret |= (ncr->cur_bus & 0xff);
            if (ncr->icr & ICR_SEL)
                ret |= BUS_SEL;
            if (ncr->icr & ICR_BSY)
                ret |= BUS_BSY;
            // if ((ret & SCSI_PHASE_MESSAGE_IN) == SCSI_PHASE_MESSAGE_IN)
                // ret &= ~BUS_REQ;
            break;

        case 5: /* Bus and Status register */
            ncr5380_log("Read: Bus and Status register\n");
            ret = 0;

            bus = ncr5380_get_bus_host(ncr);
            ncr5380_log("Get host from Interrupt\n");

            /*Check if the phase in process matches with TCR's*/
            if ((bus & SCSI_PHASE_MESSAGE_IN) == (ncr->cur_bus & SCSI_PHASE_MESSAGE_IN)) {
                ncr5380_log("Phase match\n");
                ret |= STATUS_PHASE_MATCH;
            }

            ncr5380_bus_read(ncr);
            bus = ncr->cur_bus;

            if ((bus & BUS_ACK) || (ncr->icr & ICR_ACK))
                ret |= STATUS_ACK;
            if ((bus & BUS_ATN) || (ncr->icr & ICR_ATN))
                ret |= 0x02;

            if ((bus & BUS_REQ) && (ncr->mode & MODE_DMA)) {
                ncr5380_log("Entering DMA mode\n");
                ret |= STATUS_DRQ;

                bus_state = 0;

                if (bus & BUS_IO)
                    bus_state |= TCR_IO;
                if (bus & BUS_CD)
                    bus_state |= TCR_CD;
                if (bus & BUS_MSG)
                    bus_state |= TCR_MSG;
                if ((ncr->tcr & 7) != bus_state) {
                    ncr5380_irq(ncr, 1);
                    ncr5380_log("IRQ issued\n");
                }
            }
            if (!(bus & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
                ncr5380_log("Busy error\n");
                ret |= STATUS_BUSY_ERROR;
            }
            ret |= (ncr->isr & (STATUS_INT | STATUS_END_OF_DMA));
            break;

        case 6:
            ret = ncr->tx_data;
            break;

        case 7: /* reset Parity/Interrupt */
            ncr->isr &= ~(STATUS_BUSY_ERROR | 0x20);
            ncr5380_irq(ncr, 0);
            ncr5380_log("Reset Interrupt\n");
            break;

        default:
            ncr5380_log("NCR5380: bad read %04x\n", port);
            break;
    }

    ncr5380_log("NCR5380 read(%04x)=%02x\n", port & 7, ret);

    return ret;
}
