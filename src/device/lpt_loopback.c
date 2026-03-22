/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of parallel port loopback devices.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *          Copyright 2026 Jasmine Iwanek.
 */
// https://www.huinck.net/kabel/menu_Cable.html
// https://www.huinck.net/kabel/ca_ParallelPortLoopbackNorton.html
// https://www.huinck.net/kabel/ca_ParallelPortLoopbackCheckIt.html
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef ENABLE_LOOPBACK_LOG
#include <stdarg.h>
#define HAVE_STDARG_H
#endif
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/plat_unused.h>

/*
 * Pin		Name					Signal Type		Register Bit	Note
 * 1		Strobe					/Output			Control Bit 0	Inverted. 1 = Data is valid (Clock).
 * 2		Data 0					Output			Data Bit 0
 * 3		Data 1					Output			Data Bit 1
 * 4		Data 2					Output			Data Bit 2
 * 5		Data 3					Output			Data Bit 3
 * 6		Data 4					Output			Data Bit 4
 * 7		Data 5					Output			Data Bit 5
 * 8		Data 6					Output			Data Bit 6
 * 9		Data 7					Output			Data Bit 7
 * 10		Acknowledge				/Input			Status Bit 6	0 = Pulse received.
 * 11		Busy					Input			Status Bit 7	Inverted. 0 = Busy, 1 = Ready.
 * 12		P.End (Out of Paper)	Input			Status Bit 5	1 = Out of paper.
 * 13		Select					Input			Status Bit 4	1 = Printer is online.
 * 14		Autofeed				/Output			Control Bit 1	Inverted. 1 = Auto line feed.
 * 15		Error					/Input			Status Bit 3	0 = Error/Offline.
 * 16		Initialize Printer		/Output			Control Bit 2	Pulse Low to reset printer.
 * 17		Select Input			/Output			Control Bit 3	Inverted. Selects the printer.
 */

#define LOOPBACK_TYPE_CHECKIT 0
#define LOOPBACK_TYPE_NORTON  1

/* Data Register (Output) Bit Definitions */
#define LPT_DATA_D0          0x01 /* Pin 2 */
#define LPT_DATA_D1          0x02 /* Pin 3 */
#define LPT_DATA_D2          0x04 /* Pin 4 */
#define LPT_DATA_D3          0x08 /* Pin 5 */
#define LPT_DATA_D4          0x10 /* Pin 6 */
#define LPT_DATA_D5          0x20 /* Pin 7 */
#define LPT_DATA_D6          0x40 /* Pin 8 */
#define LPT_DATA_D7          0x80 /* Pin 9 */

/* Status Register (Input) Bit Definitions - $379h */
#define LPT_STATUS_TIMEOUT       0x01  /* Bit 0: indicates a timeout (EPP only) */
#define LPT_STATUS_BIT1_RESERVED 0x02  /* Bit 1: Reserved */
#define LPT_STATUS_IRQ           0x04  /* Bit 2: */
#define LPT_STATUS_ERROR         0x08  /* Bit 3: Pin 15 */
#define LPT_STATUS_SELECT_IN     0x10  /* Bit 4: Pin 13 */
#define LPT_STATUS_PAPER_OUT     0x20  /* Bit 5: Pin 12 */
#define LPT_STATUS_ACK           0x40  /* Bit 6: Pin 10 */
#define LPT_STATUS_BUSY          0x80  /* Bit 7: Pin 11 (Inverted in Hardware) */

/* Default "Floating" Status (LPT Hardware usually pulls Busy high) */
#define LPT_STATUS_DEFAULT   0x80

/* Control Register (Output) Bit Definitions - $37Ah */
#define LPT_CTRL_STROBE      0x01  /* Bit 0: Strobe, Pin 1                        */
#define LPT_CTRL_AUTO_LF     0x02  /* Bit 1: Auto Linefeed, Pin 14                */
#define LPT_CTRL_INIT        0x04  /* Bit 2: Initialize Printer (Reset), Pin 16   */
#define LPT_CTRL_SLCT_IN     0x08  /* Bit 3: Select Printer, Pin 17               */
#define LPT_CTRL_IRQ_EN      0x10  /* Bit 4: Interrupt Enable                     */
#define LPT_CTRL_DIR_READ    0x20  /* Bit 5: 1 = Input (Read), 0 = Output (Write) */

/* ECR - Extended Control Register bits */
#define LPT_ECR_FIFO_EMPTY   0x01  /* Bit 0: FIFO is empty */
#define LPT_ECR_FIFO_FULL    0x02  /* Bit 1: FIFO is full */
#define LPT_ECR_SERVICE_INTR 0x04  /* Bit 2: Service Interrupt */
#define LPT_ECR_DMA_ENABLE   0x08  /* Bit 3: DMA Enable */
#define LPT_ECR_ERR_INTR_EN  0x10  /* Bit 4: nErrIntrEn (Interrupt on Error) */

/* ECP Operation Modes (Bits 5-7 of ECR) */
#define LPT_ECR_MODE_SPP     0x00  /* Standard Parallel Port mode */
#define LPT_ECR_MODE_BYTE    0x20  /* PS/2 Byte mode (Bidirectional) */
#define LPT_ECR_MODE_EPP     0x80  /* EPP mode */
#define LPT_ECR_MODE_ECP     0x60  /* ECP mode */
#define LPT_ECR_MODE_FIFO    0x40  /* Parallel Port FIFO mode */
#define LPT_ECR_MODE_TEST    0xC0  /* Test mode */
#define LPT_ECR_MODE_CONFIG  0xE0  /* Configuration mode */

/* EPP Registers (Offsets from Base) */
#define LPT_EPP_ADDR_OFFSET  0x03  /* Base + 3: EPP Address Port */
#define LPT_EPP_DATA_OFFSET  0x04  /* Base + 4: EPP Data Port (4 bytes wide) */

typedef struct loopback_t {
    void *lpt;

    uint8_t data_reg;
    uint8_t status_reg;
    uint8_t ctrl_reg;

    uint8_t type;
} loopback_t;

#ifdef ENABLE_LOOPBACK_LOG
int loopback_do_log = ENABLE_LOOPBACK_LOG;

static void
loopback_log(const char *fmt, ...)
{
    va_list ap;

    if (loopback_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define loopback_log(fmt, ...)
#endif

static void
loopback_update_status(loopback_t *dev)
{
#ifdef ENABLE_LOOPBACK_LOG
    lpt_t  *bus        = (lpt_t *) dev->lpt;
#endif
    uint8_t s          = 0;
    uint8_t data       = dev->data_reg;
    uint8_t ctrl       = dev->ctrl_reg;
    uint8_t old_status = dev->status_reg;

    lpt_irq(dev->lpt, 0);

    if (dev->type == LOOPBACK_TYPE_CHECKIT) {
        // Pin 1 -> Pin 13
        if (!(ctrl & LPT_CTRL_STROBE))  s |= LPT_STATUS_SELECT_IN;
        // Pin 2 -> Pin 15
        if (data & LPT_DATA_D0)      s |= LPT_STATUS_ERROR;
        // Pin 14 -> Pin 12
        if (!(ctrl & LPT_CTRL_AUTO_LF)) s |= LPT_STATUS_PAPER_OUT;
        // Pin 16 -> Pin 10
        if (ctrl & LPT_CTRL_INIT)    s |= LPT_STATUS_ACK;
        // Pin 17 -> Pin 11
        if (ctrl & LPT_CTRL_SLCT_IN) s |= LPT_STATUS_BUSY;
    } else if (dev->type == LOOPBACK_TYPE_NORTON) {
        // Pin 2 -> Pin 15
        if (data & LPT_DATA_D0) s |= LPT_STATUS_ERROR;
        // Pin 3 -> Pin 13
        if (data & LPT_DATA_D1) s |= LPT_STATUS_SELECT_IN;
        // Pin 4 -> Pin 12
        if (data & LPT_DATA_D2) s |= LPT_STATUS_PAPER_OUT;
        // Pin 5 -> Pin 10
        if (data & LPT_DATA_D3) s |= LPT_STATUS_ACK;
        // Pin 6 -> Pin 11
        if (!(data & LPT_DATA_D4)) s |= LPT_STATUS_BUSY;
    }

// XXX: Works fine, but is it needed?
#if 0
    s |= LPT_STATUS_TIMEOUT;
    s |= LPT_STATUS_BIT1_RESERVED;
    s |= LPT_STATUS_IRQ;
#endif

    dev->status_reg = s;

    loopback_log("Loopback %d: IRQ State: %d (Enabled: %d)\n", (bus->id + 1), bus->irq_state, bus->enable_irq);

    if (ctrl & LPT_CTRL_IRQ_EN) {
        loopback_log("Loopback %d: Interrupt bit was set\n", (bus->id + 1));
        if (!(old_status & LPT_STATUS_ACK) && (dev->status_reg & LPT_STATUS_ACK)) {
            lpt_irq(dev->lpt, 1);
            loopback_log("Loopback %d: IRQ Raised (Rising Edge)\n", (bus->id + 1));
        } else if ((old_status & LPT_STATUS_ACK) && !(dev->status_reg & LPT_STATUS_ACK)) {
            loopback_log("Loopback %d: IRQ Raised (Falling Edge)\n", (bus->id + 1));
        } else {
            loopback_log("Loopback %d: IRQ Not Raised\n", (bus->id + 1));
        }
    } else {
        loopback_log("Loopback %d: Interrupt bit was not set\n", (bus->id + 1));
    }

    /* Helper to visualize bits for the log */
    loopback_log("Loopback %d: Data:%d%d%d%d%d%d%d%d (%02X)   Ctrl:%d%d%d%d%d%d%d%d (%02X)   Status:%d%d%d%d%d%d%d%d (%02X)\n", (bus->id + 1),
        (data & 0x80) ? 1 : 0, (data & 0x40) ? 1 : 0, (data & 0x20) ? 1 : 0, (data & 0x10) ? 1 : 0,
        (data & 0x08) ? 1 : 0, (data & 0x04) ? 1 : 0, (data & 0x02) ? 1 : 0, (data & 0x01) ? 1 : 0, data,
        (ctrl & 0x80) ? 1 : 0, (ctrl & 0x40) ? 1 : 0, (ctrl & 0x20) ? 1 : 0, (ctrl & 0x10) ? 1 : 0,
        (ctrl & 0x08) ? 1 : 0, (ctrl & 0x04) ? 1 : 0, (ctrl & 0x02) ? 1 : 0, (ctrl & 0x01) ? 1 : 0, ctrl,
           (s & 0x80) ? 1 : 0,    (s & 0x40) ? 1 : 0,    (s & 0x20) ? 1 : 0,    (s & 0x10) ? 1 : 0,
           (s & 0x08) ? 1 : 0,    (s & 0x04) ? 1 : 0,    (s & 0x02) ? 1 : 0,    (s & 0x01) ? 1 : 0, dev->status_reg);
}

static void
loopback_write_data(uint8_t val, void *priv)
{
    loopback_t *dev = (loopback_t *) priv;
#ifdef ENABLE_LOOPBACK_LOG
    lpt_t      *bus = (lpt_t *) dev->lpt;
#endif

// TEST this
#if 0
    /* If Direction Bit (Bit 5) is 0, allow the write.
       If it's 1, the port is in 'Read' mode and the write is ignored. */
    if (!(dev->ctrl_reg & LPT_CTRL_DIR_READ)) {
        loopback_log("Loopback %d: Data Write %02X (Direction is WRITE)\n", (bus->id + 1), val);
#endif
        loopback_log("Loopback %d: Data Write %02X\n", (bus->id + 1), val);

        dev->data_reg = val;

        loopback_update_status(dev);
// TEST this
#if 0
    } else {
        loopback_log("Loopback %d: Data Write %02X ignored (Direction is READ)\n", (bus->id + 1), val);
    }
#endif
}

static void
loopback_write_ctrl(uint8_t val, void *priv)
{
    loopback_t *dev = (loopback_t *) priv;
#ifdef ENABLE_LOOPBACK_LOG
    lpt_t      *bus = (lpt_t *) dev->lpt;
#endif
    loopback_log("Loopback %d: Ctrl Write %02X\n", (bus->id + 1), val);

// XXX: Works fine, but is it needed?
#if 0
    val |= LPT_CTRL_DIR_READ;
    val |= 0x040;
    val |= 0x080;
#endif

    dev->ctrl_reg = val;

    loopback_update_status(dev);
}

#if 0
static void
loopback_strobe(uint8_t old, uint8_t val,void *priv)
{
    loopback_t *dev = (loopback_t *) priv;
#ifdef ENABLE_LOOPBACK_LOG
    lpt_t      *bus = (lpt_t *) dev->lpt;
#endif
    loopback_log("Loopback %d: Strobe old:%02X val:%02X\n", (bus->id + 1), old, val);
}
#endif

static uint8_t
loopback_read_status(void *priv)
{
    const loopback_t *dev = (loopback_t *) priv;
#ifdef ENABLE_LOOPBACK_LOG
    lpt_t            *bus = (lpt_t *) dev->lpt;
#endif
    loopback_log("Loopback %d: Status Read %02X\n", (bus->id + 1), dev->status_reg);

    return dev->status_reg;
}

#if 0
static uint8_t
loopback_read_ctrl(void *priv)
{
    const loopback_t *dev = (loopback_t *) priv;
#ifdef ENABLE_LOOPBACK_LOG
    lpt_t            *bus = (lpt_t *) dev->lpt;
#endif
    loopback_log("Loopback %d: Ctrl Read %02X\n", (bus->id + 1), dev->ctrl_reg);

    return dev->ctrl_reg;
}
#endif

static void *
loopback_init(UNUSED(const device_t *info))
{
    loopback_t *dev = (loopback_t *) calloc(1, sizeof(loopback_t));

    if (!dev) {
        loopback_log("Loopback: Failed to allocate memory for device context\n");
        return NULL;
    }

    dev->type = device_get_config_int("type");

    dev->lpt = lpt_attach(loopback_write_data,
                          loopback_write_ctrl,
                          NULL, // *strobe
                          loopback_read_status,
                          NULL, // *read_ctrl
                          NULL, // *epp_write_data
                          NULL, // *epp_request_read
                          dev);

    if (dev->lpt) {
#ifdef ENABLE_LOOPBACK_LOG
        lpt_t *lpt_bus = (lpt_t *) dev->lpt;
#endif
        loopback_log("Loopback %d: Initializing... Type=%d (%s)\n", (lpt_bus->id + 1),
                     dev->type, (dev->type == LOOPBACK_TYPE_CHECKIT) ? "IBM/CheckIt" : "Norton");

        loopback_log("Loopback %d: Successfully attached to LPT bus: Bus Info -> IRQ:%d DMA:%d ECR:%02X Mode:%02X\n",
                     (lpt_bus->id + 1),
                     lpt_bus->irq, lpt_bus->dma, lpt_bus->ecr, (lpt_bus->ecr >> 5));

        dev->status_reg = LPT_STATUS_DEFAULT;

        loopback_log("Loopback %d: Ready. Initial Status set to %02X\n", (lpt_bus->id + 1), dev->status_reg);
    } else
        loopback_log("Loopback: CRITICAL ERROR - Failed to attach to LPT bus!\n");

    return dev;
}

static void
loopback_close(void *priv)
{
    loopback_t *dev = (loopback_t *) priv;

    if (dev) {
#ifdef ENABLE_LOOPBACK_LOG
        lpt_t *bus = (lpt_t *) dev->lpt;
#endif
        loopback_log("Loopback %d: Closing device and freeing resources.\n", (bus->id + 1));

        free(dev);
    }
}

static const device_config_t loopback_config[] = {
    {
        .name         = "type",
        .description  = "Loopback Type",
        .type         = CONFIG_SELECTION,
        .default_int  = 0,
        .selection    = {
            { .description = "CheckIt / IBM Diagnostic", .value = LOOPBACK_TYPE_CHECKIT },
            { .description = "Norton",                   .value = LOOPBACK_TYPE_NORTON  },
            { NULL                                                                      }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t lpt_loopback_device = {
    .name          = "Loopback Plug (LPT)",
    .internal_name = "lpt_loopback",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = loopback_init,
    .close         = loopback_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = loopback_config
};
