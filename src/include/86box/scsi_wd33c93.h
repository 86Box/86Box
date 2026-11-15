/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Western Digital WD33C93 SCSI controller.
 *
 * Authors: WNT50, <wnt2195@gmail.com>
 *
 *          Copyright 2026 WNT50.
 */
#ifndef EMU_SCSI_WD33C93_H
#define EMU_SCSI_WD33C93_H

#include <stdint.h>
#include <86box/scsi_device.h>

/* Register indices (register 0x00 through 0x1F). */
enum {
    WD_OWN_ID             = 0x00, /* Own ID (read/Reset) / CDB size (write) */
    WD_CONTROL            = 0x01,
    WD_TIMEOUT            = 0x02,
    WD_CDB1               = 0x03,
    WD_CDB2               = 0x04,
    WD_CDB3               = 0x05,
    WD_CDB4               = 0x06,
    WD_CDB5               = 0x07,
    WD_CDB6               = 0x08,
    WD_CDB7               = 0x09,
    WD_CDB8               = 0x0a,
    WD_CDB9               = 0x0b,
    WD_CDB10              = 0x0c,
    WD_CDB11              = 0x0d,
    WD_CDB12              = 0x0e,
    WD_TARGET_LUN         = 0x0f,
    WD_COMMAND_PHASE      = 0x10,
    WD_SYNC_TRANSFER      = 0x11,
    WD_TRANSFER_COUNT_MSB = 0x12,
    WD_TRANSFER_COUNT_MID = 0x13,
    WD_TRANSFER_COUNT_LSB = 0x14,
    WD_DEST_ID            = 0x15,
    WD_SOURCE_ID          = 0x16,
    WD_SCSI_STATUS        = 0x17, /* CSR */
    WD_COMMAND            = 0x18,
    WD_DATA               = 0x19,
    WD_QUEUE_TAG          = 0x1a, /* WD33C93B only */
    WD_INVALID_1B         = 0x1b,
    WD_INVALID_1C         = 0x1c,
    WD_INVALID_1D         = 0x1d,
    WD_INVALID_1E         = 0x1e,
    WD_AUX_STATUS         = 0x1f, /* ASR */
    WD_REG_COUNT          = 0x20
};

/* Own ID register (0x00). */
#define WD_OWN_ID_MASK      0x07
#define WD_OWN_ID_EAF       0x08 /* Enable Advanced Features (93A/93B) */
#define WD_OWN_ID_EHP       0x10 /* Enable Host Parity (93A/93B) */
#define WD_OWN_ID_RAF       0x20 /* Really Advanced Features (93B) */
#define WD_OWN_ID_FS        0xc0 /* Frequency Select (93A/93B) */
#define WD_OWN_ID_CDB_SIZE  0x0f

/* Control register (0x01). */
#define WD_CONTROL_HSP      0x01
#define WD_CONTROL_HA       0x02
#define WD_CONTROL_IDI      0x04
#define WD_CONTROL_EDI      0x08
#define WD_CONTROL_HHP      0x10
#define WD_CONTROL_DM       0xe0
#define WD_CONTROL_DM_POLL  0x00 /* 000: polled I/O */
#define WD_CONTROL_DM_BURST 0x20 /* 001: demand-mode DMA */
#define WD_CONTROL_DM_BUS   0x40 /* 010: WD bus */
#define WD_CONTROL_DM2      0x80 /* DM2, reserved */

/* Target LUN register (0x0f). */
#define WD_LUN_MASK  0x07
#define WD_LUN_TRN   0x20
#define WD_LUN_DOK   0x40
#define WD_LUN_TLV   0x80

/* Destination ID register (0x15). */
#define WD_DEST_MASK 0x07
#define WD_DEST_TG   0x18
#define WD_DEST_DF   0x20
#define WD_DEST_DPD  0x40
#define WD_DEST_SCC  0x80

/* Source ID register (0x16). */
#define WD_SRC_MASK  0x07
#define WD_SRC_SIV   0x08
#define WD_SRC_DSP   0x20
#define WD_SRC_ES    0x40
#define WD_SRC_ER    0x80

/* Auxiliary Status Register (ASR, 0x1f). */
#define WD_ASR_DBR   0x01 /* Data Buffer Ready */
#define WD_ASR_PE    0x02 /* Parity Error */
#define WD_ASR_FFE   0x04 /* FIFO Full/Empty (93B) */
#define WD_ASR_CIP   0x10 /* Command In Progress (93B) */
#define WD_ASR_BSY   0x20
#define WD_ASR_LCI   0x40
#define WD_ASR_INT   0x80

/* SCSI Status Register (CSR, 0x17) values. */
#define WD_CSR_RESET                   0x00
#define WD_CSR_RESET_EAF               0x01
#define WD_CSR_SELECT_SUCCESS          0x11
#define WD_CSR_COMMAND_SUCCESS         0x13
#define WD_CSR_SELECT_TRANSFER_SUCCESS 0x16
#define WD_CSR_TRANSFER_SUCCESS        0x18
#define WD_CSR_TRANSFER_INFO           0x20
#define WD_CSR_DISCONNECT              0x85
#define WD_CSR_INVALID_COMMAND         0x40
#define WD_CSR_SELECTION_TIMEOUT       0x42
#define WD_CSR_PARITY_ERROR            0x43
#define WD_CSR_UNEXPECTED_PHASE        0x48

/* Command register (0x18). */
#define WD_CMD_MASK                    0x7f
#define WD_CMD_RESET                   0x00
#define WD_CMD_ABORT                   0x01
#define WD_CMD_ASSERT_ATN              0x02
#define WD_CMD_NEGATE_ACK              0x03
#define WD_CMD_DISCONNECT              0x04
#define WD_CMD_RESELECT                0x05
#define WD_CMD_SELECT_ATN              0x06
#define WD_CMD_SELECT                  0x07
#define WD_CMD_SELECT_ATN_TRANSFER     0x08
#define WD_CMD_SELECT_TRANSFER         0x09
#define WD_CMD_RESELECT_RECEIVE        0x0a
#define WD_CMD_RESELECT_SEND           0x0b
#define WD_CMD_WAIT_SELECT_RECEIVE     0x0c
#define WD_CMD_SEND_STATUS_COMPLETE    0x0d
#define WD_CMD_SEND_DISCONNECT         0x0e
#define WD_CMD_SET_IDI                 0x0f
#define WD_CMD_TRANSLATE_ADDRESS       0x18
#define WD_CMD_TRANSFER_PAD            0x19
#define WD_CMD_TRANSFER_INFO           0x20
#define WD_CMD_SBT                     0x80

/* Command Phase register (0x10) values. */
#define WD_PHASE_ZERO                  0x00
#define WD_PHASE_SELECTED              0x10
#define WD_PHASE_IDENTIFY              0x20
#define WD_PHASE_CDB_BYTES_0           0x30
#define WD_PHASE_CDB_BYTES_12          0x3c
#define WD_PHASE_SAVE_DATA_POINTERS    0x41
#define WD_PHASE_DISCONNECT            0x42
#define WD_PHASE_DISCONNECTED          0x43
#define WD_PHASE_RESELECTED            0x44
#define WD_PHASE_TRANSFER_COUNT        0x46
#define WD_PHASE_RECEIVE_STATUS        0x47
#define WD_PHASE_STATUS_RECEIVED       0x50
#define WD_PHASE_COMMAND_COMPLETE      0x60

/* Host interface mode. */
enum {
    WD_IFACE_DIRECT = 0,
    WD_IFACE_INDIRECT,
    WD_IFACE_INDIRECT_SEPARATE
};

/* Host data transfer mode. */
enum {
    WD_TX_PIO = 0,
    WD_TX_DMA_IN,
    WD_TX_DMA_OUT
};

/* Internal command state (not a SCSI phase). */
enum {
    WD_STATE_IDLE = 0,
    WD_STATE_TRANSFER,
    WD_STATE_FINISHED
};

typedef struct wd33c93_t {
    /* Register file and indirect address latch. */
    uint8_t regs[WD_REG_COUNT];
    uint8_t addr;
    uint8_t iface;

    /* CDB size written to register 0x00. */
    uint8_t cdbsize;
    uint8_t own_id;

    /* Data FIFO (12 bytes on the 93A/93B). */
    uint8_t fifo[12];
    uint8_t fifo_r;
    uint8_t fifo_w;
    uint8_t fifo_cnt;

    /* Interrupt (CSR) queue, 8 entries. */
    uint8_t csr_queue[8];
    uint8_t csr_r;
    uint8_t csr_w;
    uint8_t csr_cnt;

    /* Command state machine. */
    uint8_t cmd;
    uint8_t state;
    uint8_t tx_mode;
    uint8_t status;

    /* Transfer counter. */
    uint32_t xfer_count;
    uint32_t data_pos;

    /* Bus and board-side data. */
    uint8_t bus;

    /* Injected by the board layer. */
    void  *priv;
    void (*irq_ena)(void *priv, int state);
    int (*dma_read)(void *priv, uint8_t *val);
    int (*dma_write)(void *priv, uint8_t val);

    scsi_bus_t scsibus;
} wd33c93_t;

#define WD_CSR_SIZE   8
#define WD_FIFO_SIZE  12

/* Interface exported to the board layer. */
uint8_t wd33c93_port_read(wd33c93_t *wd, uint16_t off);
void    wd33c93_port_write(wd33c93_t *wd, uint16_t off, uint8_t val);
void    wd33c93_service_dma(wd33c93_t *wd);
void    wd33c93_reset(wd33c93_t *wd);

#endif /*EMU_SCSI_WD33C93_H*/
