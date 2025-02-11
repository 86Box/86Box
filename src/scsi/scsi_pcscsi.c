/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Tekram DC-390 SCSI and related MCA
 *          controllers using the NCR 53c9x series of chips.
 *
 *
 *
 * Authors: Fabrice Bellard (QEMU)
 *          Herve Poussineau (QEMU)
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2005-2018 Fabrice Bellard.
 *          Copyright 2012-2018 Herve Poussineau.
 *          Copyright 2017-2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/mca.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_pcscsi.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/fifo8.h>

#define DC390_ROM        "roms/scsi/esp_pci/INT13.BIN"
#define AM53C974_ROM     "roms/scsi/esp_pci/harom.bin"

#define ESP_REGS         16
#define ESP_FIFO_SZ      16
#define ESP_CMDFIFO_SZ   32

#define ESP_TCLO         0x0
#define ESP_TCMID        0x1
#define ESP_FIFO         0x2
#define ESP_CMD          0x3
#define ESP_RSTAT        0x4
#define ESP_WBUSID       0x4
#define ESP_RINTR        0x5
#define ESP_WSEL         0x5
#define ESP_RSEQ         0x6
#define ESP_WSYNTP       0x6
#define ESP_RFLAGS       0x7
#define ESP_WSYNO        0x7
#define ESP_CFG1         0x8
#define ESP_RRES1        0x9
#define ESP_WCCF         0x9
#define ESP_RRES2        0xa
#define ESP_WTEST        0xa
#define ESP_CFG2         0xb
#define ESP_CFG3         0xc
#define ESP_RES3         0xd
#define ESP_TCHI         0xe
#define ESP_RES4         0xf

#define CMD_DMA          0x80
#define CMD_CMD          0x7f

#define CMD_NOP          0x00
#define CMD_FLUSH        0x01
#define CMD_RESET        0x02
#define CMD_BUSRESET     0x03
#define CMD_TI           0x10
#define CMD_ICCS         0x11
#define CMD_MSGACC       0x12
#define CMD_PAD          0x18
#define CMD_SATN         0x1a
#define CMD_RSTATN       0x1b
#define CMD_SEL          0x41
#define CMD_SELATN       0x42
#define CMD_SELATNS      0x43
#define CMD_ENSEL        0x44
#define CMD_DISSEL       0x45

#define STAT_DO          0x00
#define STAT_DI          0x01
#define STAT_CD          0x02
#define STAT_ST          0x03
#define STAT_MO          0x06
#define STAT_MI          0x07
#define STAT_PIO_MASK    0x06

#define STAT_TC          0x10
#define STAT_PE          0x20
#define STAT_GE          0x40
#define STAT_INT         0x80

#define BUSID_DID        0x07

#define INTR_FC          0x08
#define INTR_BS          0x10
#define INTR_DC          0x20
#define INTR_ILL         0x40
#define INTR_RST         0x80

#define SEQ_0            0x0
#define SEQ_MO           0x1
#define SEQ_CD           0x4

#define CFG1_RESREPT     0x40

#define TCHI_FAS100A     0x04
#define TCHI_AM53C974    0x12

#define DMA_CMD          0x0
#define DMA_STC          0x1
#define DMA_SPA          0x2
#define DMA_WBC          0x3
#define DMA_WAC          0x4
#define DMA_STAT         0x5
#define DMA_SMDLA        0x6
#define DMA_WMAC         0x7

#define DMA_CMD_MASK     0x03
#define DMA_CMD_DIAG     0x04
#define DMA_CMD_MDL      0x10
#define DMA_CMD_INTE_P   0x20
#define DMA_CMD_INTE_D   0x40
#define DMA_CMD_DIR      0x80

#define DMA_STAT_PWDN    0x01
#define DMA_STAT_ERROR   0x02
#define DMA_STAT_ABORT   0x04
#define DMA_STAT_DONE    0x08
#define DMA_STAT_SCSIINT 0x10
#define DMA_STAT_BCMBLT  0x20

#define SBAC_STATUS      (1 << 24)
#define SBAC_PABTEN      (1 << 25)

typedef struct esp_t {
    mem_mapping_t mmio_mapping;
    mem_mapping_t ram_mapping;
    char          nvr_path[64];
    uint8_t       pci_slot;
    int           has_bios;
    int           BIOSBase;
    int           MMIOBase;
    rom_t         bios;
    ati_eeprom_t  eeprom;
    int           PCIBase;

    uint8_t  rregs[ESP_REGS];
    uint8_t  wregs[ESP_REGS];
    int      irq;
    int      tchi_written;
    uint32_t ti_size;
    uint32_t status;
    uint32_t dma;
    Fifo8    fifo;
    uint8_t  bus;
    uint8_t  id, lun;
    Fifo8    cmdfifo;
    uint8_t  cmdfifo_cdb_offset;
    int      data_ready;

    int32_t  xfer_counter;
    int      dma_enabled;

    uint32_t buffer_pos;
    uint32_t dma_regs[8];
    uint32_t sbac;

    double period;

    pc_timer_t timer;

    int      local;
    int      mca;
    uint16_t Base;
    uint8_t  HostID;
    uint8_t  DmaChannel;

    struct {
        uint8_t mode;
        uint8_t status;
        int     interrupt;
        int     pos;
    } dma_86c01;

    uint8_t irq_state;
    uint8_t pos_regs[8];
} esp_t;

#define READ_FROM_DEVICE 1
#define WRITE_TO_DEVICE  0

uint8_t esp_pci_regs[256];
bar_t   esp_pci_bar[2];

#ifdef ENABLE_ESP_LOG
int esp_do_log = ENABLE_ESP_LOG;

static void
esp_log(const char *fmt, ...)
{
    va_list ap;

    if (esp_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define esp_log(fmt, ...)
#endif

static void esp_dma_enable(esp_t *dev, int level);
static void esp_do_dma(esp_t *dev);
static void esp_do_nodma(esp_t *dev);
static void esp_pci_dma_memory_rw(esp_t *dev, uint8_t *buf, uint32_t len, int dir);
static void esp_timer_on(esp_t *dev, scsi_device_t *sd, double p);
static void esp_command_complete(void *priv, uint32_t status);
static void esp_dma_ti_check(esp_t *dev);
static void esp_nodma_ti_dataout(esp_t *dev);
static void esp_pci_soft_reset(esp_t *dev);
static void esp_pci_hard_reset(esp_t *dev);
static void handle_ti(esp_t *dev);

static int
esp_cdb_length(uint8_t *buf)
{
    int cdb_len;

    switch (buf[0] >> 5) {
        case 0:
        case 3:
            cdb_len = 6;
            break;
        case 1:
        case 2:
        case 6: /*Vendor unique*/
            cdb_len = 10;
            break;
        case 4:
            cdb_len = 16;
            break;
        case 5:
            cdb_len = 12;
            break;
        default:
            cdb_len = -1;
            break;
    }
    return cdb_len;
}

static void
esp_pci_update_irq(esp_t *dev)
{
    int scsi_level = !!(dev->dma_regs[DMA_STAT] & DMA_STAT_SCSIINT);
    int dma_level = (dev->dma_regs[DMA_CMD] & DMA_CMD_INTE_D) ?
                    !!(dev->dma_regs[DMA_STAT] & DMA_STAT_DONE) : 0;
    int level = scsi_level || dma_level;

    if (level) {
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        esp_log("Raising PCI IRQ...\n");
    } else {
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        esp_log("Lowering PCI IRQ...\n");
    }
}

static void
esp_irq(esp_t *dev, int level)
{
    if (dev->mca) {
        if (level) {
            picintlevel(1 << dev->irq, &dev->irq_state);
            dev->dma_86c01.mode |= 0x40;
            esp_log("Raising IRQ...\n");
        } else {
            picintclevel(1 << dev->irq, &dev->irq_state);
            dev->dma_86c01.mode &= ~0x40;
            esp_log("Lowering IRQ...\n");
        }
    } else {
        if (level) {
            dev->dma_regs[DMA_STAT] |= DMA_STAT_SCSIINT;
            /*
             * If raising the ESP IRQ to indicate end of DMA transfer, set
             * DMA_STAT_DONE at the same time. In theory this should be done in
             * esp_pci_dma_memory_rw(), however there is a delay between setting
             * DMA_STAT_DONE and the ESP IRQ arriving which is visible to the
             * guest that can cause confusion e.g. Linux
             */
            if (((dev->dma_regs[DMA_CMD] & DMA_CMD_MASK) == 0x03) &&
                (dev->dma_regs[DMA_WBC] == 0))
                dev->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
        } else
            dev->dma_regs[DMA_STAT] &= ~DMA_STAT_SCSIINT;

        esp_pci_update_irq(dev);
    }
}

static void
esp_raise_irq(esp_t *dev)
{
    if (!(dev->rregs[ESP_RSTAT] & STAT_INT)) {
        dev->rregs[ESP_RSTAT] |= STAT_INT;
        esp_irq(dev, 1);
    }
}

static void
esp_lower_irq(esp_t *dev)
{
    if (dev->rregs[ESP_RSTAT] & STAT_INT) {
        dev->rregs[ESP_RSTAT] &= ~STAT_INT;
        esp_irq(dev, 0);
    }
}

static void
esp_set_phase(esp_t *dev, uint8_t phase)
{
    dev->rregs[ESP_RSTAT] &= ~7;
    dev->rregs[ESP_RSTAT] |= phase;
}

static uint8_t
esp_get_phase(esp_t *dev)
{
    return dev->rregs[ESP_RSTAT] & 7;
}

static void
esp_fifo_push(esp_t *dev, uint8_t val)
{
    if (fifo8_num_used(&dev->fifo) == dev->fifo.capacity)
        return;

    fifo8_push(&dev->fifo, val);
}

static uint8_t
esp_fifo_pop(esp_t *dev)
{
    uint8_t val;

    if (fifo8_is_empty(&dev->fifo))
        val = 0;
    else
        val = fifo8_pop(&dev->fifo);

    return val;
}

static uint32_t
esp_fifo_pop_buf(esp_t *dev, uint8_t *dest, int maxlen)
{
    uint32_t len = fifo8_pop_buf(&dev->fifo, dest, maxlen);

    return len;
}

static uint32_t
esp_get_tc(esp_t *dev)
{
    uint32_t dmalen;

    dmalen = dev->rregs[ESP_TCLO] & 0xff;
    dmalen |= dev->rregs[ESP_TCMID] << 8;
    dmalen |= dev->rregs[ESP_TCHI] << 16;

    return dmalen;
}

static void
esp_set_tc(esp_t *dev, uint32_t dmalen)
{
    uint32_t old_tc = esp_get_tc(dev);

    dev->rregs[ESP_TCLO]  = dmalen & 0xff;
    dev->rregs[ESP_TCMID] = dmalen >> 8;
    dev->rregs[ESP_TCHI]  = dmalen >> 16;

    esp_log("OLDTC=%d, DMALEN=%d.\n", old_tc, dmalen);
    if (old_tc && !dmalen)
        dev->rregs[ESP_RSTAT] |= STAT_TC;
}

static uint32_t
esp_get_stc(esp_t *dev)
{
    uint32_t dmalen;

    dmalen = dev->wregs[ESP_TCLO] & 0xff;
    dmalen |= (dev->wregs[ESP_TCMID] << 8);
    dmalen |= (dev->wregs[ESP_TCHI] << 16);

    esp_log("STCW=%d.\n", dmalen);
    return dmalen;
}

static int
esp_select(esp_t *dev)
{
    scsi_device_t *sd;

    dev->id = dev->wregs[ESP_WBUSID] & BUSID_DID;
    sd = &scsi_devices[dev->bus][dev->id];

    dev->ti_size = 0;
    dev->rregs[ESP_RSEQ] = SEQ_0;

    if (!scsi_device_present(sd)) {
        esp_log("ESP SCSI no devices on ID %d, LUN %d\n", dev->id, dev->lun);
        /* No such drive */
        dev->rregs[ESP_RSTAT] = 0;
        dev->rregs[ESP_RINTR] = INTR_DC;
        esp_raise_irq(dev);
        return -1;
    } else
        esp_log("ESP SCSI device present on ID %d, LUN %d\n", dev->id, dev->lun);

    return 0;
}

 /* Callback to indicate that the SCSI layer has completed a transfer.  */
static void
esp_transfer_data(esp_t *dev)
{
    if (!dev->data_ready) {
        dev->data_ready = 1;

        switch (dev->rregs[ESP_CMD]) {
            case CMD_SEL:
            case (CMD_SEL | CMD_DMA):
            case CMD_SELATN:
            case (CMD_SELATN | CMD_DMA):
                /*
                 * Initial incoming data xfer is complete for sequencer command
                 * so raise deferred bus service and function complete interrupt
                 */
                 dev->rregs[ESP_RINTR] |= (INTR_BS | INTR_FC);
                 dev->rregs[ESP_RSEQ] = SEQ_CD;
                 break;

            case CMD_SELATNS:
            case (CMD_SELATNS | CMD_DMA):
                /*
                 * Initial incoming data xfer is complete so raise command
                 * completion interrupt
                 */
                 dev->rregs[ESP_RINTR] |= INTR_BS;
                 dev->rregs[ESP_RSEQ] = SEQ_MO;
                 break;

            case CMD_TI:
            case (CMD_TI | CMD_DMA):
                /*
                 * Bus service interrupt raised because of initial change to
                 * DATA phase
                 */
                dev->rregs[ESP_CMD] = 0;
                dev->rregs[ESP_RINTR] |= INTR_BS;
                break;
        }

        esp_raise_irq(dev);
    }

    /*
     * Always perform the initial transfer upon reception of the next TI
     * command to ensure the DMA/non-DMA status of the command is correct.
     * It is not possible to use s->dma directly in the section below as
     * some OSs send non-DMA NOP commands after a DMA transfer. Hence if the
     * async data transfer is delayed then s->dma is set incorrectly.
     */

    if (dev->rregs[ESP_CMD] == (CMD_TI | CMD_DMA)) {
        /* When the SCSI layer returns more data, raise deferred INTR_BS */
        esp_dma_ti_check(dev);
        esp_do_dma(dev);
    } else if (dev->rregs[ESP_CMD] == CMD_TI)
        esp_do_nodma(dev);
}


static void
esp_do_command_phase(esp_t *dev)
{
    uint32_t       cmdlen;
    uint8_t        buf[ESP_CMDFIFO_SZ];
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][dev->id];

    sd->buffer_length = -1;

    cmdlen = fifo8_num_used(&dev->cmdfifo);
    if (!cmdlen)
        return;

    fifo8_pop_buf(&dev->cmdfifo, buf, cmdlen);

    for (int i = 0; i < cmdlen; i++)
        esp_log("CDB[%i] = %02x\n", i, buf[i]);

    scsi_device_command_phase0(sd, buf);

    dev->buffer_pos   = 0;
    dev->ti_size      = sd->buffer_length;
    dev->xfer_counter = sd->buffer_length;

    esp_log("ESP SCSI Command = 0x%02x, ID = %d, LUN = %d, len = %d, phase = %02x.\n", buf[0], dev->id, dev->lun, sd->buffer_length, sd->phase);

    fifo8_reset(&dev->cmdfifo);

    dev->data_ready = 0;
    if (sd->buffer_length > 0) {
        if (sd->phase == SCSI_PHASE_DATA_IN) {
            esp_set_phase(dev, STAT_DI);
            esp_log("ESP Data In\n");
            esp_timer_on(dev, sd, scsi_device_get_callback(sd));
        } else if (sd->phase == SCSI_PHASE_DATA_OUT) {
            esp_set_phase(dev, STAT_DO);
            dev->ti_size = -sd->buffer_length;
            esp_log("ESP Data Out\n");
            esp_timer_on(dev, sd, scsi_device_get_callback(sd));
        }
        esp_log("ESP SCSI Start reading/writing\n");
        esp_do_dma(dev);
    } else {
        esp_log("ESP SCSI Command with no length\n");
        esp_command_complete(dev, sd->status);
    }
    esp_transfer_data(dev);
}


static void
esp_do_message_phase(esp_t *dev)
{
    int     len;
    uint8_t message;

    if (dev->cmdfifo_cdb_offset) {
        message = fifo8_is_empty(&dev->cmdfifo) ? 0 :
                          fifo8_pop(&dev->cmdfifo);

        dev->lun = message & 7;
        dev->cmdfifo_cdb_offset--;

        if (scsi_device_present(&scsi_devices[dev->bus][dev->id]) && (dev->lun > 0)) {
            /* We only support LUN 0 */
            esp_log("LUN = %i\n", dev->lun);
            dev->rregs[ESP_RSTAT] = 0;
            dev->rregs[ESP_RINTR] = INTR_DC;
            dev->rregs[ESP_RSEQ]  = SEQ_0;
            esp_raise_irq(dev);
            fifo8_reset(&dev->cmdfifo);
            return;
        }

        scsi_device_identify(&scsi_devices[dev->bus][dev->id], dev->lun);
    }

    esp_log("CDB offset = %i\n", dev->cmdfifo_cdb_offset);

    if (dev->cmdfifo_cdb_offset) {
        len = MIN(dev->cmdfifo_cdb_offset, fifo8_num_used(&dev->cmdfifo));
        fifo8_drop(&dev->cmdfifo, len);
        dev->cmdfifo_cdb_offset = 0;
    }
}

static void
esp_do_cmd(esp_t *dev)
{
    esp_log("DO CMD.\n");
    esp_do_message_phase(dev);
    assert(dev->cmdfifo_cdb_offset == 0);
    esp_do_command_phase(dev);
}

static void
esp_dma_enable(esp_t *dev, int level)
{
    if (level) {
        esp_log("ESP DMA Enabled\n");
        dev->dma_enabled = 1;
        timer_stop(&dev->timer);
        if (((dev->rregs[ESP_CMD] & CMD_CMD) != CMD_TI) && ((dev->rregs[ESP_CMD] & CMD_CMD) != CMD_PAD)) {
            timer_on_auto(&dev->timer, 40.0);
        } else {
            esp_log("Period = %lf\n", dev->period);
            timer_on_auto(&dev->timer, dev->period);
        }
    } else {
        esp_log("ESP DMA Disabled\n");
        dev->dma_enabled = 0;
    }
}

static void
esp_hard_reset(esp_t *dev)
{
    memset(dev->rregs, 0, ESP_REGS);
    memset(dev->wregs, 0, ESP_REGS);
    dev->ti_size      = 0;
    fifo8_reset(&dev->fifo);
    fifo8_reset(&dev->cmdfifo);
    dev->dma             = 0;
    dev->tchi_written    = 0;
    dev->rregs[ESP_CFG1] = dev->mca ? dev->HostID : 7;
    esp_log("ESP Reset\n");

    timer_stop(&dev->timer);
}

static int
esp_cdb_ready(esp_t *dev)
{
    int len = fifo8_num_used(&dev->cmdfifo) - dev->cmdfifo_cdb_offset;
    const uint8_t *pbuf;
    uint32_t n;
    int cdblen;

    if (len <= 0)
        return 0;

    pbuf = fifo8_peek_bufptr(&dev->cmdfifo, len, &n);
    if (n < len) {
        /*
         * In normal use the cmdfifo should never wrap, but include this check
         * to prevent a malicious guest from reading past the end of the
         * cmdfifo data buffer below
         */
        return 0;
    }

    cdblen = esp_cdb_length((uint8_t *)&pbuf[dev->cmdfifo_cdb_offset]);

    return (cdblen < 0) ? 0 : (len >= cdblen);
}

static void
esp_dma_ti_check(esp_t *dev)
{
    if ((esp_get_tc(dev) == 0) && (fifo8_num_used(&dev->fifo) < 2)) {
        dev->rregs[ESP_RINTR] |= INTR_BS;
        esp_raise_irq(dev);
    }
}

static void
esp_do_dma(esp_t *dev)
{
    scsi_device_t *sd  = &scsi_devices[dev->bus][dev->id];
    uint8_t  buf[ESP_CMDFIFO_SZ];
    uint32_t len;

    esp_log("ESP SCSI Actual DMA len = %d\n", esp_get_tc(dev));

    len = esp_get_tc(dev);

    switch (esp_get_phase(dev)) {
        case STAT_MO:
            len = MIN(len, fifo8_num_free(&dev->cmdfifo));
            if (dev->mca) {
                dma_set_drq(dev->DmaChannel, 1);
                while (dev->dma_86c01.pos < len) {
                    int val                   = dma_channel_read(dev->DmaChannel);
                    buf[dev->dma_86c01.pos++] = val & 0xff;
                }
                dev->dma_86c01.pos = 0;
                dma_set_drq(dev->DmaChannel, 0);
            } else
                esp_pci_dma_memory_rw(dev, buf, len, WRITE_TO_DEVICE);

            esp_set_tc(dev, esp_get_tc(dev) - len);
            fifo8_push_all(&dev->cmdfifo, buf, len);
            dev->cmdfifo_cdb_offset += len;

            switch (dev->rregs[ESP_CMD]) {
                case (CMD_SELATN | CMD_DMA):
                    if (fifo8_num_used(&dev->cmdfifo) >= 1) {
                        /* First byte received, switch to command phase */
                        esp_set_phase(dev, STAT_CD);
                        dev->rregs[ESP_RSEQ] = SEQ_CD;
                        dev->cmdfifo_cdb_offset = 1;

                        if (fifo8_num_used(&dev->cmdfifo) > 1) {
                            /* Process any additional command phase data */
                            esp_do_dma(dev);
                        }
                    }
                    break;

                case (CMD_SELATNS | CMD_DMA):
                    if (fifo8_num_used(&dev->cmdfifo) == 1) {
                        /* First byte received, stop in message out phase */
                        dev->rregs[ESP_RSEQ] = SEQ_MO;
                        dev->cmdfifo_cdb_offset = 1;

                        /* Raise command completion interrupt */
                        dev->rregs[ESP_RINTR] |= (INTR_BS | INTR_FC);
                        esp_raise_irq(dev);
                    }
                    break;

                case (CMD_TI | CMD_DMA):
                    /* ATN remains asserted until TC == 0 */
                    if (esp_get_tc(dev) == 0) {
                        esp_set_phase(dev, STAT_CD);
                        dev->rregs[ESP_CMD] = 0;
                        dev->rregs[ESP_RINTR] |= INTR_BS;
                        esp_raise_irq(dev);
                    }
                    break;

                default:
                    break;
            }
            break;

        case STAT_CD:
            len = MIN(len, fifo8_num_free(&dev->cmdfifo));
            if (dev->mca) {
                dma_set_drq(dev->DmaChannel, 1);
                while (dev->dma_86c01.pos < len) {
                    int val                   = dma_channel_read(dev->DmaChannel);
                    buf[dev->dma_86c01.pos++] = val & 0xff;
                }
                dev->dma_86c01.pos = 0;
                dma_set_drq(dev->DmaChannel, 0);
            } else
                esp_pci_dma_memory_rw(dev, buf, len, WRITE_TO_DEVICE);

            fifo8_push_all(&dev->cmdfifo, buf, len);
            esp_set_tc(dev, esp_get_tc(dev) - len);
            dev->ti_size = 0;
            if (esp_get_tc(dev) == 0) {
                /* Command has been received */
                esp_do_cmd(dev);
            }
            break;

        case STAT_DO:
            if (!dev->xfer_counter && esp_get_tc(dev)) {
                /* Defer until data is available.  */
                return;
            }
            if (len > dev->xfer_counter)
                len = dev->xfer_counter;

            switch (dev->rregs[ESP_CMD]) {
                case (CMD_TI | CMD_DMA):
                    if (dev->mca) {
                        dma_set_drq(dev->DmaChannel, 1);
                        while (dev->dma_86c01.pos < len) {
                            int val = dma_channel_read(dev->DmaChannel);
                            esp_log("ESP SCSI DMA write for 53C9x: pos = %i, val = %02x\n", dev->dma_86c01.pos, val & 0xff);
                            sd->sc->temp_buffer[dev->buffer_pos + dev->dma_86c01.pos] = val & 0xff;
                            dev->dma_86c01.pos++;
                        }
                        dma_set_drq(dev->DmaChannel, 0);
                        dev->dma_86c01.pos = 0;
                    } else
                        esp_pci_dma_memory_rw(dev, sd->sc->temp_buffer + dev->buffer_pos, len, WRITE_TO_DEVICE);

                    esp_set_tc(dev, esp_get_tc(dev) - len);
                    dev->buffer_pos += len;
                    dev->xfer_counter -= len;
                    dev->ti_size += len;
                    break;

                case (CMD_PAD | CMD_DMA):
                    /* Copy TC zero bytes into the incoming stream */
                    memset(sd->sc->temp_buffer + dev->buffer_pos, 0, len);

                    dev->buffer_pos += len;
                    dev->xfer_counter -= len;
                    dev->ti_size += len;
                    break;

                default:
                    break;
            }

            if ((dev->xfer_counter <= 0) && (fifo8_num_used(&dev->fifo) < 2)) {
                /* Defer until the scsi layer has completed */
                if (dev->ti_size < 0) {
                    esp_log("ESP SCSI Keep writing\n");
                    esp_do_dma(dev);
                } else {
                    esp_log("ESP SCSI Write finished\n");
                    scsi_device_command_phase1(sd);
                    esp_command_complete(dev, sd->status);
                }
                return;
            }

            esp_dma_ti_check(dev);
            break;

        case STAT_DI:
            if (!dev->xfer_counter && esp_get_tc(dev)) {
                /* Defer until data is available.  */
                return;
            }

            if (len > dev->xfer_counter)
                len = dev->xfer_counter;

            switch (dev->rregs[ESP_CMD]) {
                case (CMD_TI | CMD_DMA):
                    if (dev->mca) {
                        dma_set_drq(dev->DmaChannel, 1);
                        while (dev->dma_86c01.pos < len) {
                            dma_channel_write(dev->DmaChannel, sd->sc->temp_buffer[dev->buffer_pos + dev->dma_86c01.pos]);
                            esp_log("ESP SCSI DMA read for 53C9x: pos = %i, val = %02x\n", dev->dma_86c01.pos, sd->sc->temp_buffer[dev->buffer_pos + dev->dma_86c01.pos]);
                            dev->dma_86c01.pos++;
                        }
                        dev->dma_86c01.pos = 0;
                        dma_set_drq(dev->DmaChannel, 0);
                    } else
                        esp_pci_dma_memory_rw(dev, sd->sc->temp_buffer + dev->buffer_pos, len, READ_FROM_DEVICE);

                    dev->buffer_pos += len;
                    dev->xfer_counter -= len;
                    dev->ti_size -= len;
                    esp_set_tc(dev, esp_get_tc(dev) - len);
                    break;

                case (CMD_PAD | CMD_DMA):
                    dev->buffer_pos += len;
                    dev->xfer_counter -= len;
                    dev->ti_size -= len;
                    esp_set_tc(dev, esp_get_tc(dev) - len);
                    break;

                default:
                    break;
            }

            if ((dev->xfer_counter <= 0) && (fifo8_num_used(&dev->fifo) < 2)) {
                /* Defer until the scsi layer has completed */
                if (dev->ti_size <= 0) {
                    esp_log("ESP SCSI Read finished\n");
                    scsi_device_command_phase1(sd);
                    esp_command_complete(dev, sd->status);
                } else {
                    esp_log("ESP SCSI Keep reading\n");
                    esp_do_dma(dev);
                }
                return;
            }
            esp_dma_ti_check(dev);
            break;

        case STAT_ST:
            switch (dev->rregs[ESP_CMD]) {
                case (CMD_ICCS | CMD_DMA):
                    len = MIN(len, 1);

                    if (len) {
                        buf[0] = dev->status;

                        if (dev->mca) {
                            dma_set_drq(dev->DmaChannel, 1);
                            while (dev->dma_86c01.pos < len) {
                                dma_channel_write(dev->DmaChannel, buf[dev->dma_86c01.pos]);
                                dev->dma_86c01.pos++;
                            }
                            dev->dma_86c01.pos = 0;
                            dma_set_drq(dev->DmaChannel, 0);
                        } else
                            esp_pci_dma_memory_rw(dev, buf, len, READ_FROM_DEVICE);

                        esp_set_tc(dev, esp_get_tc(dev) - len);
                        esp_set_phase(dev, STAT_MI);

                        if (esp_get_tc(dev) > 0) {
                            /* Process any message in phase data */
                            esp_do_dma(dev);
                        }
                    }
                    break;

                default:
                    /* Consume remaining data if the guest underflows TC */
                    if (fifo8_num_used(&dev->fifo) < 2) {
                        dev->rregs[ESP_RINTR] |= INTR_BS;
                        esp_raise_irq(dev);
                    }
                    break;
            }
            break;

        case STAT_MI:
            switch (dev->rregs[ESP_CMD]) {
                case (CMD_ICCS | CMD_DMA):
                    len = MIN(len, 1);

                    if (len) {
                        buf[0] = 0;

                        if (dev->mca) {
                            dma_set_drq(dev->DmaChannel, 1);
                            while (dev->dma_86c01.pos < len) {
                                dma_channel_write(dev->DmaChannel, buf[dev->dma_86c01.pos]);
                                dev->dma_86c01.pos++;
                            }
                            dev->dma_86c01.pos = 0;
                            dma_set_drq(dev->DmaChannel, 0);
                        } else
                            esp_pci_dma_memory_rw(dev, buf, len, READ_FROM_DEVICE);

                        esp_set_tc(dev, esp_get_tc(dev) - len);

                        /* Raise end of command interrupt */
                        dev->rregs[ESP_RINTR] |= INTR_FC;
                        esp_raise_irq(dev);
                    }
                    break;
            }
            break;

        default:
            break;
    }
}

static void
esp_nodma_ti_dataout(esp_t *dev)
{
    scsi_device_t *sd  = &scsi_devices[dev->bus][dev->id];
    int len;

    if (!dev->xfer_counter) {
        /* Defer until data is available.  */
        return;
    }
    len = MIN(dev->xfer_counter, ESP_FIFO_SZ);
    len = MIN(len, fifo8_num_used(&dev->fifo));
    esp_fifo_pop_buf(dev, sd->sc->temp_buffer + dev->buffer_pos, len);
    dev->buffer_pos += len;
    dev->xfer_counter -= len;
    dev->ti_size += len;

    if (dev->xfer_counter <= 0) {
        if (dev->ti_size < 0) {
            esp_log("ESP SCSI Keep writing\n");
            esp_nodma_ti_dataout(dev);
        } else {
            esp_log("ESP SCSI Write finished\n");
            scsi_device_command_phase1(sd);
            esp_command_complete(dev, sd->status);
        }
        return;
    }

    dev->rregs[ESP_RINTR] |= INTR_BS;
    esp_raise_irq(dev);
}

static void
esp_do_nodma(esp_t *dev)
{
    scsi_device_t *sd  = &scsi_devices[dev->bus][dev->id];
    uint8_t buf[ESP_FIFO_SZ];
    int len;

    switch (esp_get_phase(dev)) {
        case STAT_MO:
            switch (dev->rregs[ESP_CMD]) {
                case CMD_SELATN:
                    /* Copy FIFO into cmdfifo */
                    len = esp_fifo_pop_buf(dev, buf, fifo8_num_used(&dev->fifo));
                    len = MIN(fifo8_num_free(&dev->cmdfifo), len);
                    fifo8_push_all(&dev->cmdfifo, buf, len);

                    if (fifo8_num_used(&dev->cmdfifo) >= 1) {
                        /* First byte received, switch to command phase */
                        esp_set_phase(dev, STAT_CD);
                        dev->rregs[ESP_RSEQ] = SEQ_CD;
                        dev->cmdfifo_cdb_offset = 1;

                        if (fifo8_num_used(&dev->cmdfifo) > 1) {
                            /* Process any additional command phase data */
                            esp_do_nodma(dev);
                        }
                    }
                    break;

                case CMD_SELATNS:
                    /* Copy one byte from FIFO into cmdfifo */
                    len = esp_fifo_pop_buf(dev, buf,
                                           MIN(fifo8_num_used(&dev->fifo), 1));
                    len = MIN(fifo8_num_free(&dev->cmdfifo), len);
                    fifo8_push_all(&dev->cmdfifo, buf, len);

                    if (fifo8_num_used(&dev->cmdfifo) >= 1) {
                        /* First byte received, stop in message out phase */
                        dev->rregs[ESP_RSEQ] = SEQ_MO;
                        dev->cmdfifo_cdb_offset = 1;

                        /* Raise command completion interrupt */
                        dev->rregs[ESP_RINTR] |= (INTR_BS | INTR_FC);
                        esp_raise_irq(dev);
                    }
                    break;

                case CMD_TI:
                    /* Copy FIFO into cmdfifo */
                    len = esp_fifo_pop_buf(dev, buf, fifo8_num_used(&dev->fifo));
                    len = MIN(fifo8_num_free(&dev->cmdfifo), len);
                    fifo8_push_all(&dev->cmdfifo, buf, len);

                    /* ATN remains asserted until FIFO empty */
                    dev->cmdfifo_cdb_offset = fifo8_num_used(&dev->cmdfifo);
                    esp_set_phase(dev, STAT_CD);
                    dev->rregs[ESP_CMD] = 0;
                    dev->rregs[ESP_RINTR] |= INTR_BS;
                    esp_raise_irq(dev);
                    break;

                default:
                    break;
            }
            break;

        case STAT_CD:
            switch (dev->rregs[ESP_CMD]) {
                case CMD_TI:
                    /* Copy FIFO into cmdfifo */
                    len = esp_fifo_pop_buf(dev, buf, fifo8_num_used(&dev->fifo));
                    len = MIN(fifo8_num_free(&dev->cmdfifo), len);
                    fifo8_push_all(&dev->cmdfifo, buf, len);

                    /* CDB may be transferred in one or more TI commands */
                    if (esp_cdb_ready(dev)) {
                        /* Command has been received */
                        esp_do_cmd(dev);
                    } else {
                        /*
                         * If data was transferred from the FIFO then raise bus
                         * service interrupt to indicate transfer complete. Otherwise
                         * defer until the next FIFO write.
                         */
                        if (len) {
                            /* Raise interrupt to indicate transfer complete */
                            dev->rregs[ESP_RINTR] |= INTR_BS;
                            esp_raise_irq(dev);
                        }
                    }
                    break;

                case (CMD_SEL | CMD_DMA):
                case (CMD_SELATN | CMD_DMA):
                    /* Copy FIFO into cmdfifo */
                    len = esp_fifo_pop_buf(dev, buf, fifo8_num_used(&dev->fifo));
                    len = MIN(fifo8_num_free(&dev->cmdfifo), len);
                    fifo8_push_all(&dev->cmdfifo, buf, len);

                    /* Handle when DMA transfer is terminated by non-DMA FIFO write */
                    if (esp_cdb_ready(dev)) {
                        /* Command has been received */
                        esp_do_cmd(dev);
                    }
                    break;

                case CMD_SEL:
                case CMD_SELATN:
                    /* FIFO already contain entire CDB: copy to cmdfifo and execute */
                    len = esp_fifo_pop_buf(dev, buf, fifo8_num_used(&dev->fifo));
                    len = MIN(fifo8_num_free(&dev->cmdfifo), len);
                    fifo8_push_all(&dev->cmdfifo, buf, len);

                    esp_do_cmd(dev);
                    break;

                default:
                    break;
            }
            break;

        case STAT_DO:
            /* Accumulate data in FIFO until non-DMA TI is executed */
            break;

        case STAT_DI:
            if (!dev->xfer_counter) {
                /* Defer until data is available.  */
                return;
            }
            if (fifo8_is_empty(&dev->fifo)) {
                esp_fifo_push(dev, sd->sc->temp_buffer[dev->buffer_pos]);
                dev->buffer_pos++;
                dev->ti_size--;
                dev->xfer_counter--;
            }

            if (dev->xfer_counter <= 0) {
                if (dev->ti_size <= 0) {
                    esp_log("ESP FIFO Read finished\n");
                    scsi_device_command_phase1(sd);
                    esp_command_complete(dev, sd->status);
                } else {
                    esp_log("ESP FIFO Keep reading\n");
                    esp_do_nodma(dev);
                }
                return;
            }

            /* If preloading the FIFO, defer until TI command issued */
            if (dev->rregs[ESP_CMD] != CMD_TI)
                return;

            dev->rregs[ESP_RINTR] |= INTR_BS;
            esp_raise_irq(dev);
            break;

        case STAT_ST:
            switch (dev->rregs[ESP_CMD]) {
                case CMD_ICCS:
                    esp_fifo_push(dev, dev->status);
                    esp_set_phase(dev, STAT_MI);

                    /* Process any message in phase data */
                    esp_do_nodma(dev);
                    break;
                default:
                    break;
            }
            break;

        case STAT_MI:
            switch (dev->rregs[ESP_CMD]) {
                case CMD_ICCS:
                    esp_fifo_push(dev, 0);

                    /* Raise end of command interrupt */
                    dev->rregs[ESP_RINTR] |= INTR_FC;
                    esp_raise_irq(dev);
                    break;
                default:
                    break;
            }
            break;
    }
}

/* Callback to indicate that the SCSI layer has completed a command.  */
static void
esp_command_complete(void *priv, uint32_t status)
{
    esp_t *dev = (esp_t *) priv;
    scsi_device_t *sd  = &scsi_devices[dev->bus][dev->id];

    dev->ti_size = 0;
    dev->status = status;

    switch (dev->rregs[ESP_CMD]) {
        case CMD_SEL:
        case (CMD_SEL | CMD_DMA):
        case CMD_SELATN:
        case (CMD_SELATN | CMD_DMA):
            /*
             * Switch to status phase. For non-DMA transfers from the target the last
             * byte is still in the FIFO
             */
            dev->rregs[ESP_RINTR] |= (INTR_BS | INTR_FC);
            dev->rregs[ESP_RSEQ]  = SEQ_CD;
            break;
        case CMD_TI:
        case (CMD_TI | CMD_DMA):
            dev->rregs[ESP_CMD] = 0;
            break;
        default:
            break;
    }
    /* Raise bus service interrupt to indicate change to STATUS phase */
    scsi_device_identify(sd, SCSI_LUN_USE_CDB);

    esp_set_phase(dev, STAT_ST);
    dev->rregs[ESP_RINTR] |= INTR_BS;
    esp_raise_irq(dev);
}

static void
esp_timer_on(esp_t *dev, scsi_device_t *sd, double p)
{
    if (dev->mca) {
        /* Normal SCSI: 5000000 bytes per second */
        dev->period = (p > 0.0) ? p : (((double) sd->buffer_length) * 0.2);
    } else {
        /* Fast SCSI: 10000000 bytes per second */
        dev->period = (p > 0.0) ? p : (((double) sd->buffer_length) * 0.1);
    }

    timer_on_auto(&dev->timer, dev->period + 40.0);
}

static void
handle_pad(esp_t *dev)
{
    if (dev->dma) {
        esp_log("ESP Handle PAD, do data, minlen = %i\n", esp_get_tc(dev));
        esp_do_dma(dev);
    } else {
        esp_log("ESP Handle PAD, do nodma, minlen = %i\n", dev->xfer_counter);
        esp_do_nodma(dev);
    }
}

static void
handle_ti(esp_t *dev)
{
    if (dev->dma) {
        esp_log("ESP Handle TI, do data, minlen = %i\n", esp_get_tc(dev));
        esp_do_dma(dev);
    } else {
        esp_log("ESP Handle TI, do nodma, minlen = %i\n", dev->xfer_counter);
        esp_do_nodma(dev);
        if (esp_get_phase(dev) == STAT_DO)
            esp_nodma_ti_dataout(dev);
    }
}

static void
handle_s_without_atn(void *priv)
{
    esp_t *dev = (esp_t *) priv;

    if (esp_select(dev) < 0)
        return;

    esp_set_phase(dev, STAT_CD);
    dev->cmdfifo_cdb_offset = 0;

    if (dev->dma)
        esp_do_dma(dev);
    else
        esp_do_nodma(dev);
}

static void
handle_satn(void *priv)
{
    esp_t *dev = (esp_t *) priv;

    if (esp_select(dev) < 0)
        return;

    esp_set_phase(dev, STAT_MO);

    if (dev->dma)
        esp_do_dma(dev);
    else
        esp_do_nodma(dev);
}

static void
handle_satn_stop(void *priv)
{
    esp_t *dev = (esp_t *) priv;

    if (esp_select(dev) < 0)
        return;

    esp_set_phase(dev, STAT_MO);
    dev->cmdfifo_cdb_offset = 0;

    if (dev->dma)
        esp_do_dma(dev);
    else
        esp_do_nodma(dev);
}

static void
esp_write_response(esp_t *dev)
{
    if (dev->dma)
        esp_do_dma(dev);
    else
        esp_do_nodma(dev);
}

static void
esp_callback(void *priv)
{
    esp_t *dev = (esp_t *) priv;

    if (dev->dma_enabled || !dev->dma || ((dev->rregs[ESP_CMD] & CMD_CMD) == CMD_PAD)) {
        if ((dev->rregs[ESP_CMD] & CMD_CMD) == CMD_TI) {
            esp_log("ESP SCSI Handle TI Callback\n");
            handle_ti(dev);
        } else if ((dev->rregs[ESP_CMD] & CMD_CMD) == CMD_PAD) {
            esp_log("ESP SCSI Handle PAD Callback\n");
            handle_pad(dev);
        }
    }
}

static uint32_t
esp_reg_read(esp_t *dev, uint32_t saddr)
{
    uint32_t ret;

    switch (saddr) {
        case ESP_FIFO:
            dev->rregs[ESP_FIFO] = esp_fifo_pop(dev);
            ret                  = dev->rregs[ESP_FIFO];
            break;
        case ESP_RINTR:
            /* Clear sequence step, interrupt register and all status bits
            except TC */
            ret                   = dev->rregs[ESP_RINTR];
            dev->rregs[ESP_RINTR] = 0;
            dev->rregs[ESP_RSTAT] &= ~STAT_TC;
            esp_log("ESP SCSI Clear sequence step\n");
            esp_lower_irq(dev);
            esp_log("ESP RINTR read old val = %02x\n", ret);
            break;
        case ESP_TCHI: /* Return the unique id if the value has never been written */
            if (dev->mca) {
                ret = dev->rregs[ESP_TCHI];
            } else {
                if (!dev->tchi_written)
                    ret = TCHI_AM53C974;
                else
                    ret = dev->rregs[ESP_TCHI];
            }
            break;
        case ESP_RFLAGS:
            ret = fifo8_num_used(&dev->fifo);
            break;
        default:
            ret = dev->rregs[saddr];
            break;
    }
    esp_log("Read reg %02x = %02x\n", saddr, ret);
    return ret;
}

static void
esp_reg_write(esp_t *dev, uint32_t saddr, uint32_t val)
{
    esp_log("Write reg %02x = %02x\n", saddr, val);
    switch (saddr) {
        case ESP_TCHI:
            dev->tchi_written = 1;
            fallthrough;
        case ESP_TCLO:
        case ESP_TCMID:
            esp_log("ESP TCW reg%02x = %02x.\n", saddr, val);
            dev->rregs[ESP_RSTAT] &= ~STAT_TC;
            break;
        case ESP_FIFO:
            if (!fifo8_is_full(&dev->fifo))
                esp_fifo_push(dev, val);

            esp_do_nodma(dev);
            break;
        case ESP_CMD:
            dev->rregs[ESP_CMD] = val;

            if (val & CMD_DMA) {
                dev->dma = 1;
                /* Reload DMA counter.  */
                esp_set_tc(dev, esp_get_stc(dev));
                if (!esp_get_stc(dev)) {
                    if (dev->rregs[ESP_CFG2] & 0x40)
                        esp_set_tc(dev, 0x1000000);
                    else
                        esp_set_tc(dev, 0x10000);
                }
            } else {
                dev->dma = 0;
                esp_log("ESP Command not for DMA\n");
            }
            if (dev->mca)
                esp_dma_enable(dev, dev->dma);

            esp_log("[%04X:%08X]: ESP Command = %02x, DMA ena1 = %d, DMA ena2 = %d\n", CS, cpu_state.pc, val & (CMD_CMD | CMD_DMA), dev->dma, dev->dma_enabled);
            switch (val & CMD_CMD) {
                case CMD_NOP:
                    break;
                case CMD_FLUSH:
                    fifo8_reset(&dev->fifo);
                    timer_on_auto(&dev->timer, 10.0);
                    break;
                case CMD_RESET:
                    if (dev->mca) {
                        esp_lower_irq(dev);
                        esp_hard_reset(dev);
                    } else
                        esp_pci_soft_reset(dev);
                    break;
                case CMD_BUSRESET:
                    for (uint8_t i = 0; i < 16; i++)
                        scsi_device_reset(&scsi_devices[dev->bus][i]);

                    if (!(dev->wregs[ESP_CFG1] & CFG1_RESREPT)) {
                        dev->rregs[ESP_RINTR] |= INTR_RST;
                        esp_log("ESP Bus Reset with IRQ\n");
                        esp_raise_irq(dev);
                    }
                    break;
                case CMD_TI:
                    esp_log("Transfer Information val = %02X\n", val);
                    break;
                case CMD_ICCS:
                    esp_write_response(dev);
                    dev->rregs[ESP_RINTR] |= INTR_FC;
                    dev->rregs[ESP_RSTAT] |= STAT_MI;
                    break;
                case CMD_SEL:
                    handle_s_without_atn(dev);
                    break;
                case CMD_SELATN:
                    handle_satn(dev);
                    break;
                case CMD_SELATNS:
                    handle_satn_stop(dev);
                    break;
                case CMD_MSGACC:
                    dev->rregs[ESP_RINTR] |= INTR_DC;
                    dev->rregs[ESP_RSEQ]   = 0;
                    dev->rregs[ESP_RFLAGS] = 0;
                    esp_log("ESP SCSI MSGACC IRQ\n");
                    esp_raise_irq(dev);
                    break;
                case CMD_PAD:
                    esp_log("val = %02X\n", val);
                    timer_stop(&dev->timer);
                    timer_on_auto(&dev->timer, dev->period);
                    esp_log("ESP Transfer Pad\n");
                    break;
                case CMD_SATN:
                case CMD_RSTATN:
                    break;
                case CMD_ENSEL:
                    dev->rregs[ESP_RINTR] = 0;
                    esp_log("ESP Enable Selection, do cmd = %d\n", dev->do_cmd);
                    break;
                case CMD_DISSEL:
                    dev->rregs[ESP_RINTR] = 0;
                    esp_log("ESP Disable Selection\n");
                    esp_raise_irq(dev);
                    break;

                default:
                    break;
            }
            break;
        case ESP_WBUSID:
            esp_log("ESP BUS ID=%d.\n", val & BUSID_DID);
            break;
        case ESP_WSEL:
        case ESP_WSYNTP:
        case ESP_WSYNO:
            break;
        case ESP_CFG1:
        case ESP_CFG2:
        case ESP_CFG3:
        case ESP_RES3:
        case ESP_RES4:
            dev->rregs[saddr] = val;
            break;
        case ESP_WCCF:
        case ESP_WTEST:
            break;
        default:
            esp_log("Unhandled writeb 0x%x = 0x%x\n", saddr, val);
            break;
    }
    dev->wregs[saddr] = val;
}

static void
esp_pci_dma_memory_rw(esp_t *dev, uint8_t *buf, uint32_t len, int dir)
{
    uint32_t sg_pos = 0;
    uint32_t addr;
    int expected_dir;

    if (dev->dma_regs[DMA_CMD] & DMA_CMD_DIR)
        expected_dir = READ_FROM_DEVICE;
    else
        expected_dir = WRITE_TO_DEVICE;

    if (dir != expected_dir) {
        esp_log("ESP unexpected direction\n");
        return;
    }

    if (dev->dma_regs[DMA_CMD] & DMA_CMD_MDL) {
        if (dev->dma_regs[DMA_STC]) {
            if (dev->dma_regs[DMA_WBC] > len)
                dev->dma_regs[DMA_WBC] = len;

            esp_log("WAC MDL=%08x, STC=%d, ID=%d.\n", dev->dma_regs[DMA_WAC] | (dev->dma_regs[DMA_WMAC] & 0xff000), dev->dma_regs[DMA_STC], dev->id);
            for (uint32_t i = 0; i < len; i++) {
                addr = dev->dma_regs[DMA_WAC];

                if (expected_dir)
                    dma_bm_write(addr | (dev->dma_regs[DMA_WMAC] & 0xff000), &buf[sg_pos], len, 4);
                else
                    dma_bm_read(addr | (dev->dma_regs[DMA_WMAC] & 0xff000), &buf[sg_pos], len, 4);

                sg_pos++;
                dev->dma_regs[DMA_WBC]--;
                dev->dma_regs[DMA_WAC]++;

                if (dev->dma_regs[DMA_WAC] & 0x1000) {
                    dev->dma_regs[DMA_WAC] = 0;
                    dev->dma_regs[DMA_WMAC] += 0x1000;
                }

                if (dev->dma_regs[DMA_WBC] <= 0) {
                    dev->dma_regs[DMA_WBC] = 0;
                    dev->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
                }
            }
        }
    } else {
        if (dev->dma_regs[DMA_WBC] < len)
            len = dev->dma_regs[DMA_WBC];

        addr = dev->dma_regs[DMA_WAC];

        if (expected_dir)
            dma_bm_write(addr, buf, len, 4);
        else
            dma_bm_read(addr, buf, len, 4);

        /* update status registers */
        dev->dma_regs[DMA_WBC] -= len;
        dev->dma_regs[DMA_WAC] += len;

        if (dev->dma_regs[DMA_WBC] == 0)
            dev->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
    }
}

static uint32_t
esp_pci_dma_read(esp_t *dev, uint16_t saddr)
{
    uint32_t ret;

    ret = dev->dma_regs[saddr];

    if (saddr == DMA_STAT) {
        if (!(dev->sbac & SBAC_STATUS)) {
            dev->dma_regs[DMA_STAT] &= ~(DMA_STAT_ERROR | DMA_STAT_ABORT | DMA_STAT_DONE);
            esp_log("ESP PCI DMA Read done cleared\n");
            esp_pci_update_irq(dev);
        }
    }

    esp_log("ESP PCI DMA Read regs addr = %04x, temp = %06x\n", saddr, ret);
    return ret;
}

static void
esp_pci_dma_write(esp_t *dev, uint16_t saddr, uint32_t val)
{
    uint32_t mask;

    switch (saddr) {
        case DMA_CMD:
            dev->dma_regs[DMA_CMD] = val;
            esp_log("ESP PCI DMA Write CMD = %02x\n", val & DMA_CMD_MASK);
            switch (val & DMA_CMD_MASK) {
                case 0: /*IDLE*/
                    esp_dma_enable(dev, 0);
                    esp_log("PCI DMA disable\n");
                    break;
                case 1: /*BLAST*/
                    dev->dma_regs[DMA_STAT] |= DMA_STAT_BCMBLT;
                    break;
                case 2: /*ABORT*/
                    scsi_device_command_stop(&scsi_devices[dev->bus][dev->id]);
                    break;
                case 3: /*START*/
                    dev->dma_regs[DMA_WAC] = dev->dma_regs[DMA_SPA];
                    dev->dma_regs[DMA_WMAC] = dev->dma_regs[DMA_SMDLA] & 0xfffffffc;
                    if (!dev->dma_regs[DMA_STC])
                        dev->dma_regs[DMA_STC] = 0x1000000;

                    dev->dma_regs[DMA_WBC]  = dev->dma_regs[DMA_STC];
                    dev->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT | DMA_STAT_DONE | DMA_STAT_ABORT | DMA_STAT_ERROR | DMA_STAT_PWDN);
                    esp_dma_enable(dev, 1);
                    esp_log("PCI DMA enable, MDL bit=%02x, SPA=%08x, SMDLA=%08x, STC=%d, ID=%d, SCSICMD=%02x.\n", val & DMA_CMD_MDL, dev->dma_regs[DMA_SPA], dev->dma_regs[DMA_SMDLA], dev->dma_regs[DMA_STC], dev->id, dev->cmdfifo.data[1]);
                    break;
                default: /* can't happen */
                    abort();
                    break;
            }
            break;
        case DMA_STC:
            dev->dma_regs[DMA_STC] = val;
            esp_log("DMASTC PCI write=%08x.\n", val);
            break;
        case DMA_SPA:
            dev->dma_regs[DMA_SPA] = val;
            esp_log("DMASPA PCI write=%08x.\n", val);
            break;
        case DMA_SMDLA:
            dev->dma_regs[DMA_SMDLA] = val;
            esp_log("DMASMDLA PCI write=%08x.\n", val);
            break;
        case DMA_STAT:
            if (dev->sbac & SBAC_STATUS) {
                /* clear some bits on write */
                mask = DMA_STAT_ERROR | DMA_STAT_ABORT | DMA_STAT_DONE;
                dev->dma_regs[DMA_STAT] &= ~(val & mask);
                esp_pci_update_irq(dev);
            }
            break;

        default:
            break;
    }
}

static void
esp_pci_soft_reset(esp_t *dev)
{
    esp_irq(dev, 0);
    dev->rregs[ESP_RSTAT] &= ~STAT_INT;
    esp_pci_hard_reset(dev);
}

static void
esp_pci_hard_reset(esp_t *dev)
{
    esp_hard_reset(dev);
    dev->dma_regs[DMA_CMD] &= ~(DMA_CMD_DIR | DMA_CMD_INTE_D | DMA_CMD_INTE_P
                                | DMA_CMD_MDL | DMA_CMD_DIAG | DMA_CMD_MASK);
    dev->dma_regs[DMA_WBC] &= ~0xffff;
    dev->dma_regs[DMA_WAC] = 0xffffffff;
    dev->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                                 | DMA_STAT_DONE | DMA_STAT_ABORT
                                 | DMA_STAT_ERROR);
    dev->dma_regs[DMA_WMAC] = 0xfffffffc;
}

static uint32_t
esp_io_pci_read(esp_t *dev, uint32_t addr, unsigned int size)
{
    uint32_t ret;

    addr &= 0x7f;

    if (addr < 0x40) {
        /* SCSI core reg */
        ret = esp_reg_read(dev, addr >> 2);
    } else if (addr < 0x60) {
        /* PCI DMA CCB */
        ret = esp_pci_dma_read(dev, (addr - 0x40) >> 2);
        esp_log("ESP PCI DMA CCB read addr = %02x, ret = %02x\n", (addr - 0x40) >> 2, ret);
    } else if (addr == 0x70) {
        /* DMA SCSI Bus and control */
        ret = dev->sbac;
        esp_log("ESP PCI SBAC read = %02x\n", ret);
    } else {
        /* Invalid region */
        ret = 0;
    }

    /* give only requested data */
    ret >>= (addr & 3) * 8;
    ret &= ~(~(uint64_t) 0 << (8 * size));

    esp_log("ESP PCI I/O read: addr = %02x, val = %02x\n", addr, ret);
    return ret;
}

static void
esp_io_pci_write(esp_t *dev, uint32_t addr, uint32_t val, unsigned int size)
{
    uint32_t current;
    uint32_t mask;
    int      shift;

    addr &= 0x7f;

    if (size < 4 || addr & 3) {
        /* need to upgrade request: we only support 4-bytes accesses */
        current = 0;

        if (addr < 0x40) {
            current = dev->wregs[addr >> 2];
        } else if (addr < 0x60) {
            current = dev->dma_regs[(addr - 0x40) >> 2];
        } else if (addr == 0x70) {
            current = dev->sbac;
        }

        shift = (4 - size) * 8;
        mask  = (~(uint32_t) 0 << shift) >> shift;

        shift = ((4 - (addr & 3)) & 3) * 8;
        val <<= shift;
        val |= current & ~(mask << shift);
        addr &= ~3;
        size = 4;
    }

    esp_log("ESP PCI I/O write: addr = %02x, val = %02x\n", addr, val);

    if (addr < 0x40) {
        /* SCSI core reg */
        esp_reg_write(dev, addr >> 2, val);
    } else if (addr < 0x60) {
        /* PCI DMA CCB */
        esp_pci_dma_write(dev, (addr - 0x40) >> 2, val);
    } else if (addr == 0x70) {
        /* DMA SCSI Bus and control */
        dev->sbac = val;
    }
}

static void
esp_pci_io_writeb(uint16_t addr, uint8_t val, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    esp_io_pci_write(dev, addr, val, 1);
}

static void
esp_pci_io_writew(uint16_t addr, uint16_t val, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    esp_io_pci_write(dev, addr, val, 2);
}

static void
esp_pci_io_writel(uint16_t addr, uint32_t val, void *priv)
{
    esp_t *dev = (esp_t *) priv;
    esp_io_pci_write(dev, addr, val, 4);
}

static uint8_t
esp_pci_io_readb(uint16_t addr, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    return esp_io_pci_read(dev, addr, 1);
}

static uint16_t
esp_pci_io_readw(uint16_t addr, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    return esp_io_pci_read(dev, addr, 2);
}

static uint32_t
esp_pci_io_readl(uint16_t addr, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    return esp_io_pci_read(dev, addr, 4);
}

static void
esp_io_set(esp_t *dev, uint32_t base, uint16_t len)
{
    esp_log("ESP: [PCI] Setting I/O handler at %04X\n", base);
    io_sethandler(base, len,
                  esp_pci_io_readb, esp_pci_io_readw, esp_pci_io_readl,
                  esp_pci_io_writeb, esp_pci_io_writew, esp_pci_io_writel, dev);
}

static void
esp_io_remove(esp_t *dev, uint32_t base, uint16_t len)
{
    esp_log("ESP: [PCI] Removing I/O handler at %04X\n", base);
    io_removehandler(base, len,
                     esp_pci_io_readb, esp_pci_io_readw, esp_pci_io_readl,
                     esp_pci_io_writeb, esp_pci_io_writew, esp_pci_io_writel, dev);
}

static void
esp_bios_set_addr(esp_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->bios.mapping, base, 0x8000);
}

static void
esp_bios_disable(esp_t *dev)
{
    mem_mapping_disable(&dev->bios.mapping);
}

#define EE_ADAPT_SCSI_ID                64
#define EE_MODE2                        65
#define EE_DELAY                        66
#define EE_TAG_CMD_NUM                  67
#define EE_ADAPT_OPTIONS                68
#define EE_BOOT_SCSI_ID                 69
#define EE_BOOT_SCSI_LUN                70
#define EE_CHKSUM1                      126
#define EE_CHKSUM2                      127

#define EE_ADAPT_OPTION_F6_F8_AT_BOOT   0x01
#define EE_ADAPT_OPTION_BOOT_FROM_CDROM 0x02
#define EE_ADAPT_OPTION_INT13           0x04
#define EE_ADAPT_OPTION_SCAM_SUPPORT    0x08

/*To do: make this separate from the SCSI card*/
static void
dc390_save_eeprom(esp_t *dev)
{
    FILE *fp = nvr_fopen(dev->nvr_path, "wb");
    if (!fp)
        return;
    fwrite(dev->eeprom.data, 1, 128, fp);
    fclose(fp);
}

static void
dc390_write_eeprom(esp_t *dev, int ena, int clk, int dat)
{
    /*Actual EEPROM is the same as the one used by the ATI cards, the 93cxx series.*/
    ati_eeprom_t *eeprom  = &dev->eeprom;
    uint8_t       tick    = eeprom->count;
    uint8_t       eedo    = eeprom->out;
    uint16_t      address = eeprom->address;
    uint8_t       command = eeprom->opcode;

    esp_log("EEPROM CS=%02x,SK=%02x,DI=%02x,DO=%02x,tick=%d\n",
            ena, clk, dat, eedo, tick);

    if (!eeprom->oldena && ena) {
        esp_log("EEPROM Start chip select cycle\n");
        tick    = 0;
        command = 0;
        address = 0;
    } else if (eeprom->oldena && !ena) {
        if (!eeprom->wp) {
            uint8_t subcommand = address >> 4;
            if (command == 0 && subcommand == 2) {
                esp_log("EEPROM Erase All\n");
                for (address = 0; address < 64; address++)
                    eeprom->data[address] = 0xffff;
                dc390_save_eeprom(dev);
            } else if (command == 3) {
                esp_log("EEPROM Erase Word\n");
                eeprom->data[address] = 0xffff;
                dc390_save_eeprom(dev);
            } else if (tick >= 26) {
                if (command == 1) {
                    esp_log("EEPROM Write Word\n");
                    eeprom->data[address] &= eeprom->dat;
                    dc390_save_eeprom(dev);
                } else if (command == 0 && subcommand == 1) {
                    esp_log("EEPROM Write All\n");
                    for (address = 0; address < 64; address++)
                        eeprom->data[address] &= eeprom->dat;
                    dc390_save_eeprom(dev);
                }
            }
        }
        eedo = 1;
        esp_log("EEPROM DO read\n");
    } else if (ena && !eeprom->oldclk && clk) {
        if (tick == 0) {
            if (dat == 0) {
                esp_log("EEPROM Got correct 1st start bit, waiting for 2nd start bit (1)\n");
                tick++;
            } else {
                esp_log("EEPROM Wrong 1st start bit (is 1, should be 0)\n");
                tick = 2;
            }
        } else if (tick == 1) {
            if (dat != 0) {
                esp_log("EEPROM Got correct 2nd start bit, getting command + address\n");
                tick++;
            } else {
                esp_log("EEPROM 1st start bit is longer than needed\n");
            }
        } else if (tick < 4) {
            tick++;
            command <<= 1;
            if (dat)
                command += 1;
        } else if (tick < 10) {
            tick++;
            address = (address << 1) | dat;
            if (tick == 10) {
                esp_log("EEPROM command = %02x, address = %02x (val = %04x)\n", command,
                        address, eeprom->data[address]);
                if (command == 2)
                    eedo = 0;
                address = address % 64;
                if (command == 0) {
                    switch (address >> 4) {
                        case 0:
                            esp_log("EEPROM Write disable command\n");
                            eeprom->wp = 1;
                            break;
                        case 1:
                            esp_log("EEPROM Write all command\n");
                            break;
                        case 2:
                            esp_log("EEPROM Erase all command\n");
                            break;
                        case 3:
                            esp_log("EEPROM Write enable command\n");
                            eeprom->wp = 0;
                            break;

                        default:
                            break;
                    }
                } else {
                    esp_log("EEPROM Read, write or erase word\n");
                    eeprom->dat = eeprom->data[address];
                }
            }
        } else if (tick < 26) {
            tick++;
            if (command == 2) {
                esp_log("EEPROM Read Word\n");
                eedo = ((eeprom->dat & 0x8000) != 0);
            }
            eeprom->dat <<= 1;
            eeprom->dat += dat;
        } else {
            esp_log("EEPROM Additional unneeded tick, not processed\n");
        }
    }

    eeprom->count   = tick;
    eeprom->oldena  = ena;
    eeprom->oldclk  = clk;
    eeprom->out     = eedo;
    eeprom->address = address;
    eeprom->opcode  = command;
    esp_log("EEPROM EEDO = %d\n", eeprom->out);
}

static void
dc390_load_eeprom(esp_t *dev)
{
    ati_eeprom_t *eeprom = &dev->eeprom;
    uint8_t      *nvr    = (uint8_t *) eeprom->data;
    int           i;
    uint16_t      checksum = 0;
    FILE         *fp;

    eeprom->out = 1;

    fp = nvr_fopen(dev->nvr_path, "rb");
    if (fp) {
        esp_log("EEPROM Load\n");
        if (fread(nvr, 1, 128, fp) != 128)
            fatal("dc390_eeprom_load(): Error reading data\n");
        fclose(fp);
    } else {
        for (i = 0; i < 16; i++) {
            nvr[i * 2]     = 0x57;
            nvr[i * 2 + 1] = 0x00;
        }

        esp_log("EEPROM Defaults\n");

        nvr[EE_ADAPT_SCSI_ID] = 7;
        nvr[EE_MODE2]         = 0x0f;
        nvr[EE_TAG_CMD_NUM]   = 0x04;
        nvr[EE_ADAPT_OPTIONS] = EE_ADAPT_OPTION_F6_F8_AT_BOOT | EE_ADAPT_OPTION_BOOT_FROM_CDROM | EE_ADAPT_OPTION_INT13;
        for (i = 0; i < EE_CHKSUM1; i += 2) {
            checksum += ((nvr[i] & 0xff) | (nvr[i + 1] << 8));
            esp_log("Checksum calc = %04x, nvr = %02x\n", checksum, nvr[i]);
        }

        checksum        = 0x1234 - checksum;
        nvr[EE_CHKSUM1] = checksum & 0xff;
        nvr[EE_CHKSUM2] = checksum >> 8;
        esp_log("EEPROM Checksum = %04x\n", checksum);
    }
}

static uint8_t
esp_pci_read(UNUSED(int func), int addr, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    // esp_log("ESP PCI: Reading register %02X\n", addr & 0xff);

    switch (addr) {
        case 0x00:
            // esp_log("ESP PCI: Read DO line = %02x\n", dev->eeprom.out);
            if (!dev->has_bios || dev->local)
                return 0x22;
            else {
                if (dev->eeprom.out)
                    return 0x22;
                else {
                    dev->eeprom.out = 1;
                    return 2;
                }
            }
            break;
        case 0x01:
            return 0x10;
        case 0x02:
            return 0x20;
        case 0x03:
            return 0x20;
        case 0x04:
            return esp_pci_regs[0x04] | 0x80; /*Respond to IO*/
        case 0x05:
            return esp_pci_regs[0x05];
        case 0x07:
            return esp_pci_regs[0x07] | 0x02;
        case 0x08:
            return 0x10; /*Revision ID*/
        case 0x09:
            return 0; /*Programming interface*/
        case 0x0A:
            return 0; /*devubclass*/
        case 0x0B:
            return 1; /*Class code*/
        case 0x0E:
            return 0; /*Header type */
        case 0x10:
            return esp_pci_bar[0].addr_regs[0] | 0x01; /*I/O space*/
        case 0x11:
            return esp_pci_bar[0].addr_regs[1];
        case 0x12:
            return esp_pci_bar[0].addr_regs[2];
        case 0x13:
            return esp_pci_bar[0].addr_regs[3];
        case 0x30:
            if (!dev->has_bios)
                return 0;
            return esp_pci_bar[1].addr_regs[0];
        case 0x31:
            if (!dev->has_bios)
                return 0;
            return esp_pci_bar[1].addr_regs[1];
        case 0x32:
            if (!dev->has_bios)
                return 0;
            return esp_pci_bar[1].addr_regs[2];
        case 0x33:
            if (!dev->has_bios)
                return 0;
            return esp_pci_bar[1].addr_regs[3];
        case 0x3C:
            return dev->irq;
        case 0x3D:
            return PCI_INTA;

        case 0x40 ... 0x4f:
            esp_log("ESP PCI: Read value %02X to register %02X, ID=%d\n", esp_pci_regs[addr], addr, dev->id);
            return esp_pci_regs[addr];

        default:
            break;
    }

    return 0;
}

static void
esp_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    esp_t  *dev = (esp_t *) priv;
    uint8_t valxor;
    int     eesk;
    int     eedi;

    // esp_log("ESP PCI: Write value %02X to register %02X\n", val, addr);

    if (!dev->local) {
        if ((addr >= 0x80) && (addr <= 0xFF)) {
            if (addr == 0x80) {
                eesk = val & 0x80 ? 1 : 0;
                eedi = val & 0x40 ? 1 : 0;
                dc390_write_eeprom(dev, 1, eesk, eedi);
            } else if (addr == 0xc0)
                dc390_write_eeprom(dev, 0, 0, 0);
            // esp_log("ESP PCI: Write value %02X to register %02X\n", val, addr);
            return;
        }
    }

    switch (addr) {
        case 0x04:
            valxor = (val & 0x01) ^ esp_pci_regs[addr];
            if (valxor & PCI_COMMAND_IO) {
                esp_io_remove(dev, dev->PCIBase, 0x80);
                if ((val & PCI_COMMAND_IO) && (dev->PCIBase != 0))
                    esp_io_set(dev, dev->PCIBase, 0x80);
            }
            if (dev->has_bios && (valxor & PCI_COMMAND_MEM)) {
                esp_bios_disable(dev);
                if ((val & PCI_COMMAND_MEM) && (esp_pci_bar[1].addr & 0x00000001))
                    esp_bios_set_addr(dev, dev->BIOSBase);
            }
            if (dev->has_bios)
                esp_pci_regs[addr] = val & 0x47;
            else
                esp_pci_regs[addr] = val & 0x45;
            break;
        case 0x05:
            esp_pci_regs[addr] = val & 0x01;
            break;

        case 0x07:
            esp_pci_regs[addr] &= ~(val & 0xf9);
            break;

        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* I/O Base set. */
            /* First, remove the old I/O. */
            esp_io_remove(dev, dev->PCIBase, 0x80);
            /* Then let's set the PCI regs. */
            esp_pci_bar[0].addr_regs[addr & 3] = val;
            /* Then let's calculate the new I/O base. */
            esp_pci_bar[0].addr &= 0xff80;
            dev->PCIBase = esp_pci_bar[0].addr;
            /* Log the new base. */
            // esp_log("ESP PCI: New I/O base is %04X\n" , dev->PCIBase);
            /* We're done, so get out of the here. */
            if (esp_pci_regs[4] & PCI_COMMAND_IO) {
                if (dev->PCIBase != 0) {
                    esp_io_set(dev, dev->PCIBase, 0x80);
                }
            }
            return;

        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            if (!dev->has_bios)
                return;
            /* BIOS Base set. */
            /* First, remove the old I/O. */
            esp_bios_disable(dev);
            /* Then let's set the PCI regs. */
            esp_pci_bar[1].addr_regs[addr & 3] = val;
            /* Then let's calculate the new I/O base. */
            esp_pci_bar[1].addr &= 0xffff0001;
            dev->BIOSBase = esp_pci_bar[1].addr & 0xffff0000;
            /* Log the new base. */
            // esp_log("ESP PCI: New BIOS base is %08X\n" , dev->BIOSBase);
            /* We're done, so get out of the here. */
            if ((esp_pci_regs[0x04] & PCI_COMMAND_MEM) && (esp_pci_bar[1].addr & 0x00000001))
                esp_bios_set_addr(dev, dev->BIOSBase);
            return;

        case 0x3c:
            esp_pci_regs[addr] = val;
            dev->irq           = val;
            esp_log("ESP IRQ now: %i\n", val);
            return;

        case 0x40 ... 0x4f:
            esp_log("ESP PCI: Write value %02X to register %02X, ID=%i.\n", val, addr, dev->id);
            esp_pci_regs[addr] = val;
            return;

        default:
            break;
    }
}

static void *
dc390_init(UNUSED(const device_t *info))
{
    esp_t *dev = calloc(1, sizeof(esp_t));

    dev->bus = scsi_get_bus();

    dev->local = info->local;
    dev->mca = 0;

    fifo8_create(&dev->fifo, ESP_FIFO_SZ);
    fifo8_create(&dev->cmdfifo, ESP_CMDFIFO_SZ);

    dev->PCIBase  = 0;
    dev->MMIOBase = 0;

    pci_add_card(PCI_ADD_NORMAL, esp_pci_read, esp_pci_write, dev, &dev->pci_slot);

    esp_pci_bar[0].addr_regs[0] = 1;
    esp_pci_regs[0x04]          = 3;

    dev->has_bios = device_get_config_int("bios");
    if (dev->has_bios) {
        dev->BIOSBase = 0xd0000;
        if (dev->local) {
            ;//rom_init(&dev->bios, AM53C974_ROM, 0xd0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        } else
            rom_init(&dev->bios, DC390_ROM, 0xd0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    }

    /* Enable our BIOS space in PCI, if needed. */
    if (dev->has_bios)
        esp_pci_bar[1].addr = 0xffff0000;
    else
        esp_pci_bar[1].addr = 0;

    if (dev->has_bios)
        esp_bios_disable(dev);

    if (!dev->local) {
        sprintf(dev->nvr_path, "dc390_%i.nvr", device_get_instance());

        /* Load the serial EEPROM. */
        dc390_load_eeprom(dev);
    }

    esp_pci_hard_reset(dev);
    for (uint8_t i = 0; i < 16; i++)
        scsi_device_reset(&scsi_devices[dev->bus][i]);

    timer_add(&dev->timer, esp_callback, dev, 0);

    scsi_bus_set_speed(dev->bus, 10000000.0);

    return dev;
}

static uint16_t
ncr53c9x_in(uint16_t port, void *priv)
{
    esp_t   *dev = (esp_t *) priv;
    uint16_t ret = 0xffff;

    port &= 0x1f;

    if (port >= 0x10)
        ret = esp_reg_read(dev, port - 0x10);
    else {
        switch (port) {
            case 0x02:
                ret = dev->dma_86c01.mode;
                break;

            case 0x0c:
                if (dev->dma_86c01.mode & 0x40)
                    dev->dma_86c01.status |= 0x01;
                else
                    dev->dma_86c01.status &= ~0x01;

                if (dev->dma_enabled)
                    dev->dma_86c01.status |= 0x02;
                else
                    dev->dma_86c01.status &= ~0x02;

                ret = dev->dma_86c01.status;
                break;

            default:
                break;
        }
    }

    esp_log("[%04X:%08X]: NCR53c9x DMA read port = %02x, ret = %02x, local = %d.\n\n", CS, cpu_state.pc, port, ret, dev->local);

    return ret;
}

static uint8_t
ncr53c9x_inb(uint16_t port, void *priv)
{
    return ncr53c9x_in(port, priv);
}

static uint16_t
ncr53c9x_inw(uint16_t port, void *priv)
{
    return (ncr53c9x_in(port, priv) & 0xff) | (ncr53c9x_in(port + 1, priv) << 8);
}

static void
ncr53c9x_out(uint16_t port, uint16_t val, void *priv)
{
    esp_t *dev = (esp_t *) priv;

    port &= 0x1f;

    esp_log("[%04X:%08X]: NCR53c9x DMA write port = %02x, val = %02x.\n\n", CS, cpu_state.pc, port, val);

    if (port >= 0x10)
        esp_reg_write(dev, port - 0x10, val);
    else {
        if (port == 0x02)
            dev->dma_86c01.mode = val;
    }
}

static void
ncr53c9x_outb(uint16_t port, uint8_t val, void *priv)
{
    ncr53c9x_out(port, val, priv);
}

static void
ncr53c9x_outw(uint16_t port, uint16_t val, void *priv)
{
    ncr53c9x_out(port, val & 0xff, priv);
    ncr53c9x_out(port + 1, val >> 8, priv);
}

static uint8_t
ncr53c9x_mca_read(int port, void *priv)
{
    const esp_t *dev = (esp_t *) priv;

    return (dev->pos_regs[port & 7]);
}

static void
ncr53c9x_mca_write(int port, uint8_t val, void *priv)
{
    esp_t                *dev             = (esp_t *) priv;
    static const uint16_t ncrmca_iobase[] = {
        0, 0x240, 0x340, 0x400, 0x420, 0x3240, 0x8240, 0xa240
    };

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /* This is always necessary so that the old handler doesn't remain. */
    if (dev->Base != 0) {
        io_removehandler(dev->Base, 0x20,
                         ncr53c9x_inb, ncr53c9x_inw, NULL,
                         ncr53c9x_outb, ncr53c9x_outw, NULL, dev);
    }

    /* Get the new assigned I/O base address. */
    dev->Base = ncrmca_iobase[(dev->pos_regs[2] & 0x0e) >> 1];

    /* Save the new IRQ and DMA channel values. */
    dev->irq = 3 + (2 * ((dev->pos_regs[2] & 0x30) >> 4));
    if (dev->irq == 9)
        dev->irq = 2;

    dev->DmaChannel = dev->pos_regs[3] & 0x0f;

    /*
     * Get misc SCSI config stuff.  For now, we are only
     * interested in the configured HA target ID.
     */
    dev->HostID = 6 + ((dev->pos_regs[5] & 0x20) ? 1 : 0);

    /* Initialize the device if fully configured. */
    if (dev->pos_regs[2] & 0x01) {
        if (dev->Base != 0) {
            /* Card enabled; register (new) I/O handler. */
            io_sethandler(dev->Base, 0x20,
                          ncr53c9x_inb, ncr53c9x_inw, NULL,
                          ncr53c9x_outb, ncr53c9x_outw, NULL, dev);

            esp_hard_reset(dev);
            for (uint8_t i = 0; i < 8; i++)
                scsi_device_reset(&scsi_devices[dev->bus][i]);
        }

        /* Say hello. */
        esp_log("NCR 53c9x: I/O=%04x, IRQ=%d, DMA=%d, HOST ID %i\n",
                dev->Base, dev->irq, dev->DmaChannel, dev->HostID);
    }
}

static uint8_t
ncr53c9x_mca_feedb(void *priv)
{
    const esp_t *dev = (esp_t *) priv;

    return (dev->pos_regs[2] & 0x01);
}

static void *
ncr53c9x_mca_init(const device_t *info)
{
    esp_t *dev = calloc(1, sizeof(esp_t));

    dev->bus = scsi_get_bus();

    dev->mca = 1;
    dev->local = info->local;

    fifo8_create(&dev->fifo, ESP_FIFO_SZ);
    fifo8_create(&dev->cmdfifo, ESP_CMDFIFO_SZ);

    dev->pos_regs[0] = 0x4f; /* MCA board ID */
    dev->pos_regs[1] = 0x7f;
    mca_add(ncr53c9x_mca_read, ncr53c9x_mca_write, ncr53c9x_mca_feedb, NULL, dev);

    esp_hard_reset(dev);
    for (uint8_t i = 0; i < 8; i++)
        scsi_device_reset(&scsi_devices[dev->bus][i]);

    timer_add(&dev->timer, esp_callback, dev, 0);

    scsi_bus_set_speed(dev->bus, 5000000.0);

    return dev;
}

static void
esp_close(void *priv)
{
    esp_t *dev = (esp_t *) priv;

    if (dev) {
        fifo8_destroy(&dev->fifo);
        fifo8_destroy(&dev->cmdfifo);

        free(dev);
        dev = NULL;
    }
}

static const device_config_t bios_enable_config[] = {
  // clang-format off
    {
        .name           = "bios",
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t dc390_pci_device = {
    .name          = "Tekram DC-390 PCI",
    .internal_name = "dc390",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = dc390_init,
    .close         = esp_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = bios_enable_config
};

const device_t am53c974_pci_device = {
    .name          = "AMD 53c974A PCI",
    .internal_name = "am53c974",
    .flags         = DEVICE_PCI,
    .local         = 1,
    .init          = dc390_init,
    .close         = esp_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ncr53c90a_mca_device = {
    .name          = "NCR 53c90a MCA",
    .internal_name = "ncr53c90a",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = ncr53c9x_mca_init,
    .close         = esp_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
