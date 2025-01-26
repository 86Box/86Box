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

static void
ncr5380_reset(ncr_t *ncr)
{
    scsi_bus_t *scsi_bus = &ncr->scsibus;

    ncr->output_data = 0;
    ncr->mode = 0;
    ncr->tcr = 0;
    ncr->icr = 0;
    ncr5380_log("NCR Reset\n");

    ncr->timer(ncr->priv, 0.0);

    for (int i = 0; i < 8; i++)
        scsi_device_reset(&scsi_devices[ncr->bus][i]);

    scsi_bus->state = STATE_IDLE;
    scsi_bus->clear_req = 0;
    scsi_bus->wait_complete = 0;
    scsi_bus->wait_data = 0;
    scsi_bus->bus_in = 0;
    scsi_bus->bus_out = 0;
    scsi_bus->command_pos = 0;
    scsi_bus->data_wait = 0;
    scsi_bus->data = 0;
    scsi_bus->command_issued = 0;

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
ncr5380_write(uint16_t port, uint8_t val, ncr_t *ncr)
{
    scsi_bus_t          *scsi_bus   = &ncr->scsibus;
    int                  bus_host = 0;

    ncr5380_log("NCR5380 write(%04x,%02x)\n", port & 7, val);

    switch (port & 7) {
        case 0: /* Output data register */
            ncr5380_log("[%04X:%08X]: Write: Output data register, val=%02x\n", CS, cpu_state.pc, val);
            ncr->output_data = val;
            break;

        case 1: /* Initiator Command Register */
            ncr5380_log("[%04X:%08X]: Write: Initiator command register, val=%02x.\n", CS, cpu_state.pc, val);
            if ((val & 0x80) && !(ncr->icr & 0x80)) {
                ncr5380_log("Resetting the 5380\n");
                ncr5380_reset(ncr);
                ncr5380_irq(ncr, 1);
            }
            ncr->icr = val;
            ncr5380_log("ICR WaitData=%d, ClearReq=%d.\n", scsi_bus->wait_data, scsi_bus->clear_req);
            break;

        case 2: /* Mode register */
            ncr5380_log("Write: Mode register, val=%02x.\n", val);
            if ((val & MODE_ARBITRATE) && !(ncr->mode & MODE_ARBITRATE)) {
                ncr->icr &= ~ICR_ARB_LOST;
                ncr->icr |= ICR_ARB_IN_PROGRESS;
            }
            ncr->mode = val;
            ncr->dma_mode_ext(ncr, ncr->priv, val);
            break;

        case 3: /* Target Command Register */
            ncr5380_log("Write: Target Command register, val=%02x.\n", val);
            ncr->tcr = val;
            break;

        case 4: /* Select Enable Register */
            ncr5380_log("Write: Select Enable register\n");
            break;

        case 5: /* start DMA Send */
            ncr5380_log("Write: start DMA send register\n");
            /*a Write 6/10 has occurred, start the timer when the block count is loaded*/
            scsi_bus->tx_mode = DMA_OUT_TX_BUS;
            if (ncr->dma_send_ext)
                ncr->dma_send_ext(ncr, ncr->priv);
            break;

        case 7: /* start DMA Initiator Receive */
            ncr5380_log("[%04X:%08X]: Write: start DMA initiator receive register, waitdata=%d, clearreq=%d.\n", CS, cpu_state.pc, scsi_bus->wait_data, scsi_bus->clear_req);
            /*a Read 6/10 has occurred, start the timer when the block count is loaded*/
            scsi_bus->tx_mode = DMA_IN_TX_BUS;
            if (ncr->dma_initiator_receive_ext)
                ncr->dma_initiator_receive_ext(ncr, ncr->priv);
            break;

        default:
            ncr5380_log("NCR5380: bad write %04x %02x\n", port, val);
            break;
    }

    bus_host = ncr5380_get_bus_host(ncr);
    scsi_bus_update(scsi_bus, bus_host);
}

uint8_t
ncr5380_read(uint16_t port, ncr_t *ncr)
{
    scsi_bus_t *scsi_bus    = &ncr->scsibus;
    uint8_t    ret          = 0xff;
    int        bus;
    int        bus_state;

    switch (port & 7) {
        case 0: /* Current SCSI data */
            ncr5380_log("Read: Current SCSI data register\n");
            if (ncr->icr & ICR_DBP) {
                /*Return the data from the output register if on data bus phase from ICR*/
                if (scsi_bus->command_issued) {
                    bus = scsi_bus_read(scsi_bus);
                    ret = BUS_GETDATA(bus);
                } else
                    ret = ncr->output_data;

                ncr5380_log("[%04X:%08X]: Data Bus Phase, CMDissued=%d, ret=%02x, clearreq=%d, waitdata=%x, txmode=%x.\n", CS, cpu_state.pc, scsi_bus->command_issued, ret, scsi_bus->clear_req, scsi_bus->wait_data, scsi_bus->tx_mode);
            } else {
                /*Return the data from the SCSI bus*/
                bus = scsi_bus_read(scsi_bus);
                ret = BUS_GETDATA(bus);
                ncr5380_log("[%04X:%08X]: NCR Get SCSI bus data=%02x.\n", CS, cpu_state.pc, ret);
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
            bus = scsi_bus_read(scsi_bus);
            ret |= (bus & 0xff);
            if (ncr->icr & ICR_SEL)
                ret |= BUS_SEL;
            if (ncr->icr & ICR_BSY)
                ret |= BUS_BSY;
            break;

        case 5: /* Bus and Status register */
            ncr5380_log("Read: Bus and Status register\n");
            ret = 0;

            bus = ncr5380_get_bus_host(ncr);
            ncr5380_log("Get host from Interrupt\n");

            /*Check if the phase in process matches with TCR's*/
            if ((bus & SCSI_PHASE_MESSAGE_IN) == (scsi_bus->bus_out & SCSI_PHASE_MESSAGE_IN)) {
                ncr5380_log("Phase match\n");
                ret |= STATUS_PHASE_MATCH;
            }

            bus = scsi_bus_read(scsi_bus);

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
            ncr->isr_reg = ret;
            break;

        case 6:
            ncr5380_log("Read: Input Data.\n");
            bus = scsi_bus_read(scsi_bus);
            ret = BUS_GETDATA(bus);
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
