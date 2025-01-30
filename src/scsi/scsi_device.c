/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The generic SCSI device command handler.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
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
#include <86box/device.h>
#include <86box/hdd.h>
#include <86box/hdc_ide.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/plat_unused.h>

scsi_device_t scsi_devices[SCSI_BUS_MAX][SCSI_ID_MAX];
int scsi_command_length[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };
uint8_t scsi_null_device_sense[18] = { 0x70, 0, SENSE_ILLEGAL_REQUEST, 0, 0, 0, 0, 0, 0, 0, 0, 0, ASC_INV_LUN, 0, 0, 0, 0, 0 };

#define SET_BUS_STATE(scsi_bus, state) scsi_bus->bus_out = (scsi_bus->bus_out & ~(SCSI_PHASE_MESSAGE_IN)) | (state & (SCSI_PHASE_MESSAGE_IN))

#ifdef ENABLE_SCSI_DEVICE_LOG
int scsi_device_do_log = ENABLE_SCSI_DEVICE_LOG;

static void
scsi_device_log(const char *fmt, ...)
{
    va_list ap;

    if (scsi_device_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define scsi_device_log(fmt, ...)
#endif

static uint8_t
scsi_device_target_command(scsi_device_t *dev, uint8_t *cdb)
{
    if (dev->command) {
        dev->command(dev->sc, cdb);

        if (dev->sc->tf->status & ERR_STAT)
            return SCSI_STATUS_CHECK_CONDITION;
        else
            return SCSI_STATUS_OK;
    } else
        return SCSI_STATUS_CHECK_CONDITION;
}

double
scsi_device_get_callback(scsi_device_t *dev)
{
    if (dev->sc)
        return dev->sc->callback;
    else
        return -1.0;
}

uint8_t *
scsi_device_sense(scsi_device_t *dev)
{
    if (dev->sc)
        return dev->sc->sense;
    else
        return scsi_null_device_sense;
}

void
scsi_device_request_sense(scsi_device_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    if (dev->request_sense)
        dev->request_sense(dev->sc, buffer, alloc_length);
    else
        memcpy(buffer, scsi_null_device_sense, alloc_length);
}

void
scsi_device_reset(scsi_device_t *dev)
{
    if (dev->reset)
        dev->reset(dev->sc);
}

int
scsi_device_present(scsi_device_t *dev)
{
    if (dev->type == SCSI_NONE)
        return 0;
    else
        return 1;
}

int
scsi_device_valid(scsi_device_t *dev)
{
    if (dev->sc)
        return 1;
    else
        return 0;
}

int
scsi_device_cdb_length(UNUSED(scsi_device_t *dev))
{
    /* Right now, it's 12 for all devices. */
    return 12;
}

void
scsi_device_command_phase0(scsi_device_t *dev, uint8_t *cdb)
{
    if (!dev->sc) {
        dev->phase  = SCSI_PHASE_STATUS;
        dev->status = SCSI_STATUS_CHECK_CONDITION;
        return;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    dev->phase  = SCSI_PHASE_COMMAND;
    dev->status = scsi_device_target_command(dev, cdb);
}

void
scsi_device_command_stop(scsi_device_t *dev)
{
    if (dev->command_stop) {
        dev->command_stop(dev->sc);
        dev->status = SCSI_STATUS_OK;
    }
}

void
scsi_device_command_phase1(scsi_device_t *dev)
{
    if (!dev->sc)
        return;

    /* Call the second phase. */
    if (dev->phase == SCSI_PHASE_DATA_OUT) {
        if (dev->phase_data_out)
            dev->phase_data_out(dev->sc);
    } else
        scsi_device_command_stop(dev);

    if (dev->sc->tf->status & ERR_STAT)
        dev->status = SCSI_STATUS_CHECK_CONDITION;
    else
        dev->status = SCSI_STATUS_OK;
}

/* When LUN is FF, there has been no IDENTIFY message, otherwise
   there has been one. */
void
scsi_device_identify(scsi_device_t *dev, uint8_t lun)
{
    if ((dev == NULL) || (dev->type == SCSI_NONE) || !dev->sc)
        return;

    dev->sc->cur_lun = lun;

    /* TODO: This should return a value, should IDENTIFY fail due to a
             a LUN not supported by the target. */
}

void
scsi_device_close_all(void)
{
    scsi_device_t *dev;

    for (uint8_t i = 0; i < SCSI_BUS_MAX; i++) {
        for (uint8_t j = 0; j < SCSI_ID_MAX; j++) {
            dev = &(scsi_devices[i][j]);
            if (dev->command_stop && dev->sc)
                dev->command_stop(dev->sc);
        }
    }
}

void
scsi_device_init(void)
{
    scsi_device_t *dev;

    for (uint8_t i = 0; i < SCSI_BUS_MAX; i++) {
        for (uint8_t j = 0; j < SCSI_ID_MAX; j++) {
            dev = &(scsi_devices[i][j]);

            memset(dev, 0, sizeof(scsi_device_t));
            dev->type = SCSI_NONE;
        }
    }
}

int
scsi_device_get_id(uint8_t data)
{
    for (uint8_t c = 0; c < SCSI_ID_MAX; c++) {
        if (data & (1 << c))
            return c;
    }

    return -1;
}


static int
scsi_device_get_msg(uint8_t *msgp, int len)
{
    uint8_t msg = msgp[0];
    if ((msg == 0) || ((msg >= 0x02) && (msg <= 0x1f)) || (msg >= 0x80))
        return 1;

    if ((msg >= 0x20) && (msg <= 0x2f))
        return 2;

    if (len < 2)
        return 3;

    return msgp[1];
}

int
scsi_bus_read(scsi_bus_t *scsi_bus)
{
    scsi_device_t *dev;
    int phase;

    /*Wait processes to handle bus requests*/
    if (scsi_bus->clear_req) {
        scsi_bus->clear_req--;
        if (!scsi_bus->clear_req) {
            scsi_device_log("Prelude to command data\n");
            SET_BUS_STATE(scsi_bus, scsi_bus->bus_phase);
            scsi_bus->bus_out |= BUS_REQ;
        }
    }

    if (scsi_bus->wait_data) {
        scsi_bus->wait_data--;
        if (!scsi_bus->wait_data) {
            dev = &scsi_devices[scsi_bus->bus_device][scsi_bus->target_id];
            SET_BUS_STATE(scsi_bus, scsi_bus->bus_phase);
            phase = scsi_bus->bus_out & SCSI_PHASE_MESSAGE_IN;

            switch (phase) {
                case SCSI_PHASE_DATA_IN:
                    scsi_device_log("DataIn.\n");
                    scsi_bus->state = STATE_DATAIN;
                    if ((dev->sc != NULL) && (dev->sc->temp_buffer != NULL))
                        scsi_bus->data = dev->sc->temp_buffer[scsi_bus->data_pos++];

                    scsi_bus->bus_out = (scsi_bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(scsi_bus->data) | BUS_DBP;
                    break;
                case SCSI_PHASE_DATA_OUT:
                    if (scsi_bus->bus_phase & BUS_IDLE) {
                        scsi_device_log("Bus Idle.\n");
                        scsi_bus->state = STATE_IDLE;
                        scsi_bus->bus_out &= ~BUS_BSY;
                        scsi_bus->timer(scsi_bus->priv, 0.0);
                    } else {
                        scsi_device_log("DataOut.\n");
                        scsi_bus->state = STATE_DATAOUT;
                    }
                    break;
                case SCSI_PHASE_STATUS:
                    scsi_device_log("Status.\n");
                    scsi_bus->bus_out |= BUS_REQ;
                    scsi_bus->state = STATE_STATUS;
                    scsi_bus->bus_out = (scsi_bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(dev->status) | BUS_DBP;
                    break;
                case SCSI_PHASE_MESSAGE_IN:
                    scsi_device_log("Message In.\n");
                    scsi_bus->state = STATE_MESSAGEIN;
                    scsi_bus->bus_out = (scsi_bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(0) | BUS_DBP;
                    break;
                case SCSI_PHASE_MESSAGE_OUT:
                    scsi_device_log("Message Out.\n");
                    scsi_bus->bus_out |= BUS_REQ;
                    scsi_bus->state = STATE_MESSAGEOUT;
                    scsi_bus->bus_out = (scsi_bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(scsi_bus->target_id >> 5) | BUS_DBP;
                    break;
                default:
                    break;
            }
        }
    }

    if (scsi_bus->wait_complete) {
        scsi_bus->wait_complete--;
        if (!scsi_bus->wait_complete)
            scsi_bus->bus_out |= BUS_REQ;
    }

    return scsi_bus->bus_out;
}

void
scsi_bus_update(scsi_bus_t *scsi_bus, int bus)
{
    scsi_device_t *dev = &scsi_devices[scsi_bus->bus_device][scsi_bus->target_id];
    double         p;
    uint8_t        sel_data;
    int            msglen;

    /*Start the SCSI command layer, which will also make the timings*/
    if (bus & BUS_ARB)
        scsi_bus->state = STATE_IDLE;

    scsi_device_log("State = %i\n", scsi_bus->state);

    switch (scsi_bus->state) {
        case STATE_IDLE:
            scsi_bus->clear_req = scsi_bus->wait_data = scsi_bus->wait_complete = 0;
            if ((bus & BUS_SEL) && !(bus & BUS_BSY)) {
                sel_data = BUS_GETDATA(bus);

                scsi_bus->target_id = scsi_device_get_id(sel_data);

                /*Once the device has been found and selected, mark it as busy*/
                if ((scsi_bus->target_id != (uint8_t) -1) && scsi_device_present(&scsi_devices[scsi_bus->bus_device][scsi_bus->target_id])) {
                    scsi_bus->bus_out |= BUS_BSY;
                    scsi_bus->state = STATE_SELECT;
                    scsi_device_log("Select - target ID = %i, moving to state = %d.\n", scsi_bus->target_id, scsi_bus->state);
                } else {
                    scsi_device_log("Device not found at ID %i, Current Bus BSY=%02x\n", scsi_bus->target_id, scsi_bus->bus_out);
                    scsi_bus->bus_out = 0;
                }
            }
            break;
        case STATE_SELECT:
            if (!(bus & BUS_SEL)) {
                if (!(bus & BUS_ATN)) {
                    if ((scsi_bus->target_id != (uint8_t) -1) && scsi_device_present(&scsi_devices[scsi_bus->bus_device][scsi_bus->target_id])) {
                        scsi_device_log("Device found at ID %i, Current Bus BSY=%02x\n", scsi_bus->target_id, scsi_bus->bus_out);
                        scsi_bus->state   = STATE_COMMAND;
                        scsi_bus->bus_out = BUS_BSY | BUS_REQ;
                        scsi_bus->command_pos = 0;
                        SET_BUS_STATE(scsi_bus, SCSI_PHASE_COMMAND);
                    } else {
                        scsi_device_log("Device not found at ID %i again.\n", scsi_bus->target_id);
                        scsi_bus->state   = STATE_IDLE;
                        scsi_bus->bus_out = 0;
                    }
                } else {
                    scsi_device_log("Set to SCSI Message Out\n");
                    scsi_bus->bus_phase  = SCSI_PHASE_MESSAGE_OUT;
                    scsi_bus->wait_data  = 4;
                    scsi_bus->msgout_pos = 0;
                    scsi_bus->is_msgout  = 1;
                }
            }
            break;
        case STATE_COMMAND:
            if ((bus & BUS_ACK) && !(scsi_bus->bus_in & BUS_ACK)) {
                /*Write command byte to the output data register*/
                scsi_bus->command[scsi_bus->command_pos++] = BUS_GETDATA(bus);
                scsi_bus->clear_req                   = 3;
                scsi_bus->bus_phase                   = scsi_bus->bus_out & SCSI_PHASE_MESSAGE_IN;
                scsi_bus->bus_out                    &= ~BUS_REQ;

                scsi_device_log("Command pos=%i, output data=%02x\n", scsi_bus->command_pos, BUS_GETDATA(bus));

                if (scsi_bus->command_pos == scsi_command_length[(scsi_bus->command[0] >> 5) & 7]) {
                    if (scsi_bus->is_msgout) {
                        scsi_bus->is_msgout = 0;
#if 0
                        scsi_bus->command[1] = (scsi_bus->command[1] & 0x1f) | (scsi_bus->msglun << 5);
#endif
                    }

                    /*Reset data position to default*/
                    scsi_bus->data_pos = 0;

                    dev = &scsi_devices[scsi_bus->bus_device][scsi_bus->target_id];

                    scsi_device_log("SCSI Command 0x%02X for ID %d, status code=%02x\n", scsi_bus->command[0], scsi_bus->target_id, dev->status);
                    dev->buffer_length = -1;
                    scsi_device_command_phase0(dev, scsi_bus->command);
                    scsi_device_log("SCSI ID %i: Command %02X: Buffer Length %i, SCSI Phase %02X\n", scsi_bus->target_id, scsi_bus->command[0], dev->buffer_length, dev->phase);

                    scsi_bus->period          = 1.0;
                    scsi_bus->wait_data       = 4;
                    scsi_bus->data_wait       = 0;
                    scsi_bus->command_issued  = 1;

                    if (dev->status == SCSI_STATUS_OK) {
                        /*If the SCSI phase is Data In or Data Out, allocate the SCSI buffer based on the transfer length of the command*/
                        if (dev->buffer_length && ((dev->phase == SCSI_PHASE_DATA_IN) || (dev->phase == SCSI_PHASE_DATA_OUT))) {
                            p = scsi_device_get_callback(dev);
                            scsi_bus->period = (p > 0.0) ? ((p / scsi_bus->divider) * scsi_bus->multi) : (((double) dev->buffer_length) * scsi_bus->speed);
                            scsi_device_log("SCSI ID %i: command 0x%02x for p = %lf, update = %lf, len = %i, dmamode = %x\n", scsi_bus->target_id, scsi_bus->command[0], scsi_device_get_callback(dev), scsi_bus->period, dev->buffer_length, scsi_bus->tx_mode);
                        }
                    }
                    scsi_bus->bus_phase = dev->phase;
                }
            }
            break;
        case STATE_DATAIN:
            dev = &scsi_devices[scsi_bus->bus_device][scsi_bus->target_id];
            if ((bus & BUS_ACK) && !(scsi_bus->bus_in & BUS_ACK)) {
                if (scsi_bus->data_pos >= dev->buffer_length) {
                    scsi_bus->bus_out &= ~BUS_REQ;
                    scsi_device_command_phase1(dev);
                    scsi_bus->bus_phase     = SCSI_PHASE_STATUS;
                    scsi_bus->wait_data     = 4;
                    scsi_bus->wait_complete = 8;
                } else {
                    if ((dev->sc != NULL) && (dev->sc->temp_buffer != NULL))
                        scsi_bus->data = dev->sc->temp_buffer[scsi_bus->data_pos++];

                    scsi_device_log("TXMode DataIn=%x, cmd=%02x.\n", scsi_bus->tx_mode, scsi_bus->command[0]);
                    scsi_bus->bus_out = (scsi_bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(scsi_bus->data) | BUS_DBP | BUS_REQ;
                    if (scsi_bus->tx_mode == PIO_TX_BUS) { /*If a data in command that is not read 6/10 has been issued*/
                        scsi_device_log("DMA mode idle IN=%d.\n", scsi_bus->data_pos);
                        scsi_bus->data_wait |= 1;
                        scsi_bus->timer(scsi_bus->priv, scsi_bus->period);
                    } else {
                        scsi_device_log("DMA mode IN=%d.\n", scsi_bus->data_pos);
                        scsi_bus->clear_req = 3;
                    }
                    scsi_bus->bus_out &= ~BUS_REQ;
                    scsi_bus->bus_phase = SCSI_PHASE_DATA_IN;
                }
            }
            break;
        case STATE_DATAOUT:
            dev = &scsi_devices[scsi_bus->bus_device][scsi_bus->target_id];
            if ((bus & BUS_ACK) && !(scsi_bus->bus_in & BUS_ACK)) {
                if ((dev->sc != NULL) && (dev->sc->temp_buffer != NULL))
                    dev->sc->temp_buffer[scsi_bus->data_pos++] = BUS_GETDATA(bus);

                if (scsi_bus->data_pos >= dev->buffer_length) {
                    scsi_bus->bus_out &= ~BUS_REQ;
                    scsi_device_command_phase1(dev);
                    scsi_bus->bus_phase     = SCSI_PHASE_STATUS;
                    scsi_bus->wait_data     = 4;
                    scsi_bus->wait_complete = 8;
                } else {
                    /*More data is to be transferred, place a request*/
                    if (scsi_bus->tx_mode == PIO_TX_BUS) { /*If a data in command that is not write 6/10 has been issued*/
                        scsi_device_log("DMA mode idle OUT=%d.\n", scsi_bus->data_pos);
                        scsi_bus->data_wait |= 1;
                        scsi_bus->timer(scsi_bus->priv, scsi_bus->period);
                        scsi_bus->bus_out &= ~BUS_REQ;
                    } else {
                        scsi_device_log("DMA mode OUT=%d.\n", scsi_bus->data_pos);
                        scsi_bus->bus_out |= BUS_REQ;
                    }
                }
            }
            break;
        case STATE_STATUS:
            if ((bus & BUS_ACK) && !(scsi_bus->bus_in & BUS_ACK)) {
                /*All transfers done, wait until next transfer*/
                scsi_device_identify(&scsi_devices[scsi_bus->bus_device][scsi_bus->target_id], SCSI_LUN_USE_CDB);
                scsi_bus->bus_out &= ~BUS_REQ;
                scsi_bus->bus_phase     = SCSI_PHASE_MESSAGE_IN;
                scsi_bus->wait_data     = 4;
                scsi_bus->wait_complete = 8;
                scsi_bus->command_issued = 0;
            }
            break;
        case STATE_MESSAGEIN:
            if ((bus & BUS_ACK) && !(scsi_bus->bus_in & BUS_ACK)) {
                scsi_bus->bus_out &= ~BUS_REQ;
                scsi_bus->bus_phase = BUS_IDLE;
                scsi_bus->wait_data = 4;
            }
            break;
        case STATE_MESSAGEOUT:
            if ((bus & BUS_ACK) && !(scsi_bus->bus_in & BUS_ACK)) {
                scsi_bus->msgout[scsi_bus->msgout_pos++] = BUS_GETDATA(bus);
                msglen                         = scsi_device_get_msg(scsi_bus->msgout, scsi_bus->msgout_pos);
                if (scsi_bus->msgout_pos >= msglen) {
                    if ((scsi_bus->msgout[0] & (0x80 | 0x20)) == 0x80)
                        scsi_bus->msglun = scsi_bus->msgout[0] & 7;

                    scsi_bus->bus_out &= ~BUS_REQ;
                    scsi_bus->state = STATE_MESSAGE_ID;
                }
            }
            break;
        case STATE_MESSAGE_ID:
            if ((scsi_bus->target_id != (uint8_t) -1) && scsi_device_present(&scsi_devices[scsi_bus->bus_device][scsi_bus->target_id])) {
                scsi_device_log("Device found at ID %i on MSGOUT, Current Bus BSY=%02x\n", scsi_bus->target_id, scsi_bus->bus_out);
                scsi_device_identify(&scsi_devices[scsi_bus->bus_device][scsi_bus->target_id], scsi_bus->msglun);
                scsi_bus->state   = STATE_COMMAND;
                scsi_bus->bus_out = BUS_BSY | BUS_REQ;
                scsi_bus->command_pos = 0;
                SET_BUS_STATE(scsi_bus, SCSI_PHASE_COMMAND);
            }
            break;

        default:
            break;
    }

    scsi_bus->bus_in = bus;
}

