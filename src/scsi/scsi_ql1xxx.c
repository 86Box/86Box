/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          QLogic QLA1x40/QLA1x80/QLA1x160 SCSI HBA emulation.
 *
 *          Register definitions are derived from the Matthew Jacob's
 *          multiplatform driver for ISP chipsets.
 *
 * Authors: Dmitry Borisov, <di.sean@protonmail.com>
 *
 *          Copyright 2026 Dmitry Borisov
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_qlogic.h>
#include <86box/nmc93cxx.h>
#include <86box/fifo8.h>
#include <86box/plat_unused.h>
#include <86box/plat_fallthrough.h>

#include "cpu.h"

#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])

/*
 * Device configuration
 */
#define QL_CFG_BIOS_ENABLE                  "bios"
#define QL_CFG_BIOS_REVISION                "bios_rev"

/*
 * Device info->local definitions
 */
#define QL_DEV_CHIP_TYPE_MASK        0x000000FF
#define QL_DEV_CHIP_REV_MASK         0x00000F00
#define QL_DEV_FLASH_TYPE_MASK       0x0000F000
#define QL_DEV_CHIP_REV_SHIFT        8
#define QL_DEV_FLASH_TYPE_SHIFT      12

#define QL_ISP1040                   0x00000000
#define QL_ISP1080                   0x00000001
#define QL_ISP1240                   0x00000002
#define QL_ISP1280                   0x00000003
#define QL_ISP12160                  0x00000004
#define QL_REV_ISP1080               0x00000100
#define QL_REV_ISP1020               0x00000000
#define QL_REV_ISP1020A              0x00000100
#define QL_REV_ISP1040               0x00000200
#define QL_REV_ISP1040A              0x00000300
#define QL_REV_ISP1040B              0x00000400
#define QL_REV_ISP1040C              0x00000500
#define QL_FLASH_AM29F010            0x00000000
#define QL_FLASH_AM29LV010B          0x00000100

/* ISP firmware version extracted from the BIOS */
#define ISP_FW_VER(Major, Minor, Micro)     (((Major) << 16) | ((Minor) << 8) | (Micro))

/* Address of the IOCB handler in firmware for the 1040 ISP chips */
#define QL_IOCB_FW_BASE     0x0700

/* Maximum SCSI devices supported by the chip */
#define QL_MAX_TID   16

#define QL_PCI_PM_BASE             0x44
#define QL_PCI_IO_BAR_SIZE         0x100
#define QL_PCI_MMIO_BAR_SIZE       0x1000
#define QL_PCI_ROM_BAR_64K_SIZE    0x10000
#define QL_PCI_ROM_BAR_128K_SIZE   0x20000

#define QL_IO_DECODE_MASK      (QL_PCI_IO_BAR_SIZE - 1)

/* The hardware register layout consists of a set of 2-byte aligned registers */
#define REG_TO_IDX(x)     ((x) / 2)
#define IDX_TO_REG(x)     ((x) * 2)

/*
 * Flash command definitions
 */
#define FLASH_CMD_CHIP_ERASE_CONFIRM    0x10
#define FLASH_CMD_BLOCK_ERASE_CONFIRM   0x30
#define FLASH_CMD_SETUP_ERASE           0x80
#define FLASH_CMD_AUTO_SELECT           0x90
#define FLASH_CMD_PROGRAM               0xA0
#define FLASH_CMD_READ_ARRAY            0xF0

/*
 * Flash status register definitions
 */
#define FLASH_STATUS_ERASE_TIMEOUT_EXPIRED   0x08 // DQ3
#define FLASH_STATUS_ERROR                   0x20 // DQ5
#define FLASH_STATUS_TOGGLE                  0x40 // DQ6
#define FLASH_STATUS_DATA_POLLING            0x80 // DQ7

/*
 * Flash block brotection status definitions
 */
#define FLASH_BLOCK_STATUS_NOT_PROTECTED   0x00
#define FLASH_BLOCK_STATUS_PROTECTED       0x01

/* All boards use Am29F010 or Am29LV010B, which are 128KB in size */
#define AM29_FLASH_SIZE          0x20000
#define AM29_MAX_BLOCKS          8

#define AM29_MANUFACTURER_ID     0x01 // AMD
#define AM29F010_MODEL_ID        0x20
#define AM29LV010B_MODEL_ID      0x6E

#define AM29_BLOCK_ERASE_ACCEPT_TIMEOUT_US  50.0 // 50 us

/* A0-A1 */
#define AM29_AUTOSEL_ADDR_MASK              0x3

/* Read the manufacturer ID. A0 = 0, A1 = 0 */
#define AM29_AUTOSEL_ADDR_MANUFACTURER_ID   0x0

/* Read the model ID. A0 = 1, A1 = 0 */
#define AM29_AUTOSEL_ADDR_MODEL_ID          0x1

/*
 * Hardware registers
 */
#define QL_REG_ID_LOW                  REG_TO_IDX(0x00)
#define QL_REG_ID_HIGH                 REG_TO_IDX(0x02)
#define QL_REG_CFG0                    REG_TO_IDX(0x04)
#define QL_REG_CFG1                    REG_TO_IDX(0x06)
#define QL_REG_INT_CTRL                REG_TO_IDX(0x08)
#define QL_REG_INT_STATUS              REG_TO_IDX(0x0A)
#define QL_REG_SEMAPHORE               REG_TO_IDX(0x0C)
#define QL_REG_NVRAM                   REG_TO_IDX(0x0E)
#define QL_REG_FLASH_BIOS_DATA         REG_TO_IDX(0x10)
#define QL_REG_FLASH_BIOS_ADDRESS      REG_TO_IDX(0x12)
#define QL_REG_MAILBOX0                REG_TO_IDX(0x70)
#define QL_REG_MAILBOX1                REG_TO_IDX(0x72)
#define QL_REG_MAILBOX2                REG_TO_IDX(0x74)
#define QL_REG_MAILBOX3                REG_TO_IDX(0x76)
#define QL_REG_MAILBOX4                REG_TO_IDX(0x78)
#define QL_REG_MAILBOX5                REG_TO_IDX(0x7A)
#define QL_REG_MAILBOX6                REG_TO_IDX(0x7C)
#define QL_REG_MAILBOX7                REG_TO_IDX(0x7E)

/* RISC bank registers (at offset 0x80) */
#define QL_REG_RISC_PSR                REG_TO_IDX(0x20)
#define QL_REG_RISC_PCR                REG_TO_IDX(0x24)
#define QL_REG_RISC_PC                 REG_TO_IDX(0x2C)
#define QL_REG_RISC_MTR                REG_TO_IDX(0x2E)
#define QL_REG_HOST_CMD                REG_TO_IDX(0x40)
#define QL_REG_GPIO_DATA               REG_TO_IDX(0x4C)
#define QL_REG_GPIO_ENABLE             REG_TO_IDX(0x4E)

/* SXP bank registers (at offset 0x80) */
#define QL_REG_SXP_PINS_DIFF           REG_TO_IDX(0x76)

/* DMA bank registers (at offset 0x20 for the 1040, for other chips at offset 0x80) */
#define QL_REG_CDMA_CFG                REG_TO_IDX(0x00)
#define QL_REG_CDMA_CTRL               REG_TO_IDX(0x02)
#define QL_REG_CDMA_STATUS             REG_TO_IDX(0x04)
#define QL_REG_CDMA_FIFO_STATUS        REG_TO_IDX(0x06)
#define QL_REG_CDMA_COUNT              REG_TO_IDX(0x08)
#define QL_REG_CDMA_ADDR0              REG_TO_IDX(0x0C)
#define QL_REG_CDMA_ADDR1              REG_TO_IDX(0x0E)
#define QL_REG_CDMA_ADDR2              REG_TO_IDX(0x10)
#define QL_REG_CDMA_ADDR3              REG_TO_IDX(0x12)
#define QL_REG_DDMA_CFG                REG_TO_IDX(0x20)
#define QL_REG_DDMA_CTRL               REG_TO_IDX(0x22)
#define QL_REG_DDMA_STATUS             REG_TO_IDX(0x24)
#define QL_REG_DDMA_FIFO_STATUS        REG_TO_IDX(0x26)
#define QL_REG_DDMA_XFER_COUNT_LOW     REG_TO_IDX(0x28)
#define QL_REG_DDMA_XFER_COUNT_HIGH    REG_TO_IDX(0x2A)
#define QL_REG_DDMA_ADDR0              REG_TO_IDX(0x2C)
#define QL_REG_DDMA_ADDR1              REG_TO_IDX(0x2E)
#define QL_REG_DDMA_ADDR2              REG_TO_IDX(0x30)
#define QL_REG_DDMA_ADDR3              REG_TO_IDX(0x32)
#define QL_REG_DDMA_DATA_PORT          REG_TO_IDX(0x42)

/* QL_REG_SXP_PINS_DIFF */
#define SXP_PINS_SE_MODE          0x0400
#define SXP_PINS_HVD_MODE         0x0800
#define SXP_PINS_LVD_MODE         0x1000

/* QL_REG_CFG0 */
#define BIU_CONF0_HW_MASK         0x000F /* Hardware revision mask */

#define BIU_CONF0_REV_1020        0x0000
#define BIU_CONF0_REV_1020A       0x0001
#define BIU_CONF0_REV_1040        0x0002
#define BIU_CONF0_REV_1040A       0x0003
#define BIU_CONF0_REV_1040B       0x0004
#define BIU_CONF0_REV_1040C       0x0005

#define BIU_CONF0_REV_1080        0x0001

/* QL_REG_CFG1 */
#define BIU_BURST_ENABLE          0x0004
#define BIU_PCI_CONF1_SXP         0x0008 /* SXP bank select (1040 only) */
#define BIU_PCI_CONF1_FIFO_16     0x0010
#define BIU_PCI_CONF1_FIFO_32     0x0020
#define BIU_PCI_CONF1_FIFO_64     0x0030
#define BIU_PCI_CONF1_FIFO_128    0x0040

#define BIU_PCI1080_CONF1_SXP0    0x0100 /* SXP bank #1 select */
#define BIU_PCI1080_CONF1_SXP1    0x0200 /* SXP bank #2 select */
#define BIU_PCI1080_CONF1_DMA     0x0300 /* DMA bank select */
#define BIU_PCI1080_REG_BANK_MASK 0x0700 /* Bank mask */

/* QL_REG_INT_CTRL */
#define QL_IFACE_SOFT_RESET       0x0001
#define QL_IFACE_FLASH_ENABLE     0x0100

/* QL_REG_INT_STATUS and QL_REG_INT_CTRL */
#define QL_INTR_REQ               0x0002
#define QL_RISC_INTR_REQ          0x0004

/* QL_REG_SEMAPHORE */
#define QL_SEMAPHORE_LOCK         0x0001
#define QL_SEMAPHORE_STATUS       0x0002

/* QL_REG_NVRAM */
#define QL_EEPROM_SK    0x0001
#define QL_EEPROM_CS    0x0002
#define QL_EEPROM_DI    0x0004
#define QL_EEPROM_DO    0x0008

/* QL_REG_HOST_CMD read */
#define QL_HC_FLAG_RISC_EXT      0x0010
#define QL_HC_FLAG_RISC_PAUSE    0x0020
#define QL_HC_FLAG_RISC_RESET    0x0040
#define QL_HC_FLAG_HOST_INTR     0x0080

/* QL_REG_HOST_CMD write */
#define QL_HC_RESET_RISC         0x1000
#define QL_HC_PAUSE_RISC         0x2000
#define QL_HC_RELEASE_RISC       0x3000
#define QL_HC_SET_HOST_INTR      0x5000
#define QL_HC_CLEAR_HOST_INTR    0x6000
#define QL_HC_CLEAR_RISC_INTR    0x7000
#define QL_HC_DISABLE_BIOS       0x9000

/*
 * All IOCB Queue entries are this size
 */
#define QENTRY_LEN        64

/*
 * Mailbox commands
 */
#define QL_CMD_NOP                          0x0000
#define QL_CMD_LOAD_RAM                     0x0001
#define QL_CMD_EXEC_FIRMWARE                0x0002
#define QL_CMD_DUMP_RAM                     0x0003
#define QL_CMD_WRITE_RAM_WORD               0x0004
#define QL_CMD_READ_RAM_WORD                0x0005
#define QL_CMD_REGISTER_TEST                0x0006
#define QL_CMD_VERIFY_CHECKSUM              0x0007
#define QL_CMD_ABOUT_FIRMWARE               0x0008
#define QL_CMD_LOAD_RAM_A64                 0x0009
#define QL_CMD_DUMP_RAM_A64                 0x000A
#define QL_CMD_INIT_REQ_QUEUE               0x0010
#define QL_CMD_INIT_RSP_QUEUE               0x0011
#define QL_CMD_EXECUTE_IOCB                 0x0012
#define QL_CMD_ABORT_COMMAND                0x0015
#define QL_CMD_ABORT_DEVICE                 0x0016
#define QL_CMD_ABORT_TARGET                 0x0017
#define QL_CMD_BUS_RESET                    0x0018
#define QL_CMD_START_QUEUE                  0x001A
#define QL_CMD_GET_FIRMWARE_STATUS          0x001F
#define QL_CMD_GET_RETRY_COUNT              0x0022
#define QL_CMD_GET_ACT_NEG_STATE            0x0025
#define QL_CMD_GET_TARGET_PARAMETERS        0x0028
#define QL_CMD_SET_INITIATOR_ID             0x0030
#define QL_CMD_SET_SELECTION_TIMEOUT        0x0031
#define QL_CMD_SET_RETRY_COUNT              0x0032
#define QL_CMD_SET_TAG_AGE_LIMIT            0x0033
#define QL_CMD_SET_CLOCK_RATE               0x0034
#define QL_CMD_SET_ACTIVE_NEGATION          0x0035
#define QL_CMD_SET_ASYNC_DATA_SETUP         0x0036
#define QL_CMD_SET_PCI_CONTROL              0x0037
#define QL_CMD_SET_TARGET_PARAMETERS        0x0038
#define QL_CMD_SET_DEVICE_QUEUE             0x0039
#define QL_CMD_RETURN_BIOS_BLOCK_ADDR       0x0040
#define QL_CMD_WRITE_FOUR_RAM_WORDS         0x0041
#define QL_CMD_EXEC_BIOS_IOCB               0x0042
#define QL_CMD_SET_SYSTEM_PARAMETER         0x0045
#define QL_CMD_SET_FIRMWARE_FEATURES        0x004A
#define QL_CMD_INIT_REQ_QUEUE_A64           0x0052
#define QL_CMD_INIT_RSP_QUEUE_A64           0x0053
#define QL_CMD_EXECUTE_IOCB_A64             0x0054
#define QL_CMD_GET_TRANSFER_MODE            0x0059
#define QL_CMD_SET_DATA_OVERRUN_RECOVERY    0x005A

/*
 * Mailbox command complete status codes
 */
#define QL_MBOX_STATUS_COMPLETE             0x4000
#define QL_MBOX_STATUS_INVALID              0x4001
#define QL_MBOX_STATUS_HOST_IFACE_ERROR     0x4002
#define QL_MBOX_STATUS_TEST_FAILED          0x4003
#define QL_MBOX_STATUS_CMD_ERROR            0x4005
#define QL_MBOX_STATUS_CMD_PARAM_ERROR      0x4006
#define QL_MBOX_STATUS_PENDING              0xFFFF // Invalid, for internal use only

/*
 * Mailbox asynchronous event status codes
 */
#define QL_ASYNC_STATUS_BUS_RESET           0x8001
#define QL_ASYNC_STATUS_SYSTEM_ERROR        0x8002
#define QL_ASYNC_STATUS_REQ_XFER_ERROR      0x8003
#define QL_ASYNC_STATUS_RSP_XFER_ERROR      0x8004
#define QL_ASYNC_STATUS_SCSI_CMD_COMPLETE   0x8020

/*
 * Mailbox I/O interface registers
 */
#define QL_MBOX_STATUS       0
#define QL_MBOX_HNDL_LOW     1
#define QL_MBOX_HNDL_HIGH    2
#define QL_MBOX_RQST         4
#define QL_MBOX_RESP         5
#define QL_MBOX_REGS_MAX     8

/*
 * Request and response ring helpers
 */
#define QL_RQST_CONS(dev)   ((dev)->reg_mbox_out[QL_MBOX_RQST]) // consumer (HW)
#define QL_RQST_PROD(dev)   ((dev)->reg_mbox_in[QL_MBOX_RQST])  // producer (SW)
#define QL_RESP_CONS(dev)   ((dev)->reg_mbox_in[QL_MBOX_RESP])  // consumer (SW)
#define QL_RESP_PROD(dev)   ((dev)->reg_mbox_out[QL_MBOX_RESP]) // producer (HW)

/*
 * Firmware features flags (QL_CMD_SET_FIRMWARE_FEATURES)
 */
#define FW_FEATURE_FAST_POST    0x1
#define FW_FEATURE_LVD_NOTIFY   0x2
#define FW_FEATURE_RIO_32BIT    0x4
#define FW_FEATURE_RIO_16BIT    0x8

/*
 * Request header flags definitions
 */
#define RQSFLAG_CONTINUATION    0x01
#define RQSFLAG_FULL            0x02
#define RQSFLAG_BADHEADER       0x04
#define RQSFLAG_BADPACKET       0x08
#define RQSFLAG_BADCOUNT        0x10
#define RQSFLAG_BADORDER        0x20
#define RQSFLAG_MASK            0x3F

/*
 * Request header entry_type definitions
 */
#define RQSTYPE_REQUEST         0x01
#define RQSTYPE_DATASEG         0x02
#define RQSTYPE_RESPONSE        0x03
#define RQSTYPE_MARKER          0x04
#define RQSTYPE_CMDONLY         0x05
#define RQSTYPE_ATIO            0x06 // Target Mode
#define RQSTYPE_CTIO            0x07 // Target Mode
#define RQSTYPE_REQUEST_A64     0x09
#define RQSTYPE_A64_CONT        0x0A
#define RQSTYPE_ENABLE_LUN      0x0B // Target Mode
#define RQSTYPE_MODIFY_LUN      0x0C // Target Mode
#define RQSTYPE_NOTIFY          0x0D // Target Mode
#define RQSTYPE_NOTIFY_ACK      0x0E // Target Mode
#define RQSTYPE_CTIO_A64        0x0F // Target Mode

/*
 * Request flags values
 */
#define REQFLAG_NODISCON           0x0001
#define REQFLAG_HTAG               0x0002
#define REQFLAG_OTAG               0x0004
#define REQFLAG_STAG               0x0008
#define REQFLAG_TARGET_RTN         0x0010

#define REQFLAG_NODATA             0x0000
#define REQFLAG_DATA_IN            0x0020
#define REQFLAG_DATA_OUT           0x0040
#define REQFLAG_DATA_BIDIRECTIONAL 0x0060

#define REQFLAG_DISARQ             0x0100
#define REQFLAG_FRC_ASYNC          0x0200
#define REQFLAG_FRC_SYNC           0x0400
#define REQFLAG_FRC_WIDE           0x0800
#define REQFLAG_NOPARITY           0x1000
#define REQFLAG_STOPQ              0x2000
#define REQFLAG_XTRASNS            0x4000
#define REQFLAG_PRIORITY           0x8000

/*
 * Request completion status cdes
 */
#define RQCS_COMPLETE                    0x0000
#define RQCS_INCOMPLETE                  0x0001
#define RQCS_DMA_ERROR                   0x0002
#define RQCS_TRANSPORT_ERROR             0x0003
#define RQCS_RESET_OCCURRED              0x0004
#define RQCS_ABORTED                     0x0005
#define RQCS_TIMEOUT                     0x0006
#define RQCS_DATA_OVERRUN                0x0007
#define RQCS_COMMAND_OVERRUN             0x0008
#define RQCS_STATUS_OVERRUN              0x0009
#define RQCS_BAD_MESSAGE                 0x000A
#define RQCS_NO_MESSAGE_OUT              0x000B
#define RQCS_EXT_ID_FAILED               0x000C
#define RQCS_IDE_MSG_FAILED              0x000D
#define RQCS_ABORT_MSG_FAILED            0x000E
#define RQCS_REJECT_MSG_FAILED           0x000F
#define RQCS_NOP_MSG_FAILED              0x0010
#define RQCS_PARITY_ERROR_MSG_FAILED     0x0011
#define RQCS_DEVICE_RESET_MSG_FAILED     0x0012
#define RQCS_ID_MSG_FAILED               0x0013
#define RQCS_UNEXP_BUS_FREE              0x0014
#define RQCS_DATA_UNDERRUN               0x0015
#define RQCS_XACT_ERR1                   0x0018
#define RQCS_XACT_ERR2                   0x0019
#define RQCS_XACT_ERR3                   0x001A
#define RQCS_BAD_ENTRY                   0x001B
#define RQCS_PHASE_SKIPPED               0x001D
#define RQCS_ARQS_FAILED                 0x001E
#define RQCS_WIDE_FAILED                 0x001F
#define RQCS_QUEUE_FULL                  0x001C
#define RQCS_SYNCXFER_FAILED             0x0020
#define RQCS_LVD_BUSERR                  0x0021

/*
 * Request State Flags
 */
#define RQSF_GOT_BUS            0x0100
#define RQSF_GOT_TARGET         0x0200
#define RQSF_SENT_CDB           0x0400
#define RQSF_XFRD_DATA          0x0800
#define RQSF_GOT_STATUS         0x1000
#define RQSF_GOT_SENSE          0x2000
#define RQSF_XFER_COMPLETE      0x4000

/*
 * Request Status Flags
 */
#define RQSTF_DISCONNECT        0x0001
#define RQSTF_SYNCHRONOUS       0x0002
#define RQSTF_PARITY_ERROR      0x0004
#define RQSTF_BUS_RESET         0x0008
#define RQSTF_DEVICE_RESET      0x0010
#define RQSTF_ABORTED           0x0020
#define RQSTF_TIMEOUT           0x0040
#define RQSTF_NEGOTIATION       0x0080

/*
 * Device Flags (QL_CMD_SET_TARGET_PARAMETERS, QL_CMD_GET_TARGET_PARAMETERS)
 */
#define DPARM_PPR         0x0020
#define DPARM_ASYNC       0x0040
#define DPARM_NARROW      0x0080
#define DPARM_RENEG       0x0100
#define DPARM_QFRZ        0x0200
#define DPARM_ARQ         0x0400
#define DPARM_TQING       0x0800
#define DPARM_SYNC        0x1000
#define DPARM_WIDE        0x2000
#define DPARM_PARITY      0x4000
#define DPARM_DISC        0x8000

/*
 * Generic mailbox command completion time.
 * On QLA1080 real hardware the loading of firmware image (15675 words)
 * via PIO takes approximately 1878 ms.
 */
#define QL_MBOX_GENERIC_TIME_US     110 // 110 us

/*
 * Command completion time when the SCSI device does not exists,
 * measured on QLA1080 real hardware.
 */
#define QL_CMD_SELECTION_TIMEOUT_TIME_US    370000 // 370 ms

#define SXP_FLAG_ENGINE_ACTIVE         0x00000001
#define SXP_FLAG_PICK_UP_MBOX          0x00000002
#define SXP_FLAG_FAST_POSTING          0x00000004
#define SXP_FLAG_MBOX_IOCB             0x00000008
#define SXP_FLAG_BIOS_IOCB             0x00000010
#define SXP_FLAG_WRITE_RESP_IOCB       0x00000020
#define SXP_FLAG_INC_RESP_RING         0x00000040
#define SXP_FLAG_ABORTED_CMD           0x00000080

#define QL_BIT(b)   (1 << (b))

#define ql_dma_write(address, buffer, size) dma_bm_write(address, (uint8_t *)(buffer), size, 4);
#define ql_dma_write8(address, buffer)      dma_bm_write(address, (uint8_t *)(buffer), 1, 4);
#define ql_dma_write16(address, buffer)     dma_bm_write(address, (uint8_t *)(buffer), 2, 4);
#define ql_dma_write32(address, buffer)     dma_bm_write(address, (uint8_t *)(buffer), 4, 4);

#define ql_dma_read(address, buffer, size)  dma_bm_read(address, (uint8_t *)(buffer), size, 4);
#define ql_dma_read8(address, buffer)       dma_bm_read(address, (uint8_t *)(buffer), 1, 4);
#define ql_dma_read16(address, buffer)      dma_bm_read(address, (uint8_t *)(buffer), 2, 4);
#define ql_dma_read32(address, buffer)      dma_bm_read(address, (uint8_t *)(buffer), 4, 4);

typedef enum FlashMode {
    M_READ_ARRAY,
    M_AUTO_SELECT,
    M_PROGRAM,
    M_BLOCK_ERASE,
    M_CHIP_ERASE,
    M_MAX,
} FlashMode;

typedef enum FlashBusCycleState {
    CYCLE_INVALID,
    CYCLE_CHECK_55,
    CYCLE_CHECK_AA,
    CYCLE_CHECK_FIRST_CMD,
    CYCLE_CHECK_SECOND_CMD,
    CYCLE_ENTER_PROGRAM,
} FlashBusCycleState;

typedef struct flash_block_t {
    /* Block index */
    uint32_t number;
    /* Starting address */
    uint32_t start_addr;
    /* Ending address */
    uint32_t end_addr;
    /* Block protection status */
    uint8_t protection_status;
} flash_block_t;

typedef struct flash_t {
    /* Data access cycles */
    int access_cycles;
    /* BIOS image (memory array data) */
    uint8_t *array_data;
    /* Operation mode */
    FlashMode mode;
    /* Command bus cycle */
    uint8_t bus_cycle;
    /* Command decoder state */
    uint8_t cmd_cycle;
    /* Write operation status register */
    uint8_t status_reg;
    /* Manufacturer ID */
    uint8_t manufacturer_id;
    /* Model ID */
    uint8_t model_id;
    /* Block select address mask for erase operations */
    uint32_t block_select_addr_mask;
    /* Address mask for command decode */
    uint32_t cmd_cycle_addr_mask;
    /* Physical address of the AAAA coded cycle */
    uint32_t addr_AAAA_phys;
    /* Physical address of the 5555 coded cycle */
    uint32_t addr_5555_phys;
    /* Bitmap of pending blocks for erase operation */
    uint32_t blocks_to_erase_bitmap;
    /* Byte programming time */
    double program_time_us;
    /* Block erase time */
    double block_erase_time_us;
    /* Chip erase time */
    double chip_erase_time_us;
    /* Erase timer */
    pc_timer_t erase_accept_timeout_timer;
    /* Command completion timer */
    pc_timer_t cmd_complete_timer;
    /* Blocks (sectors) */
    flash_block_t block[AM29_MAX_BLOCKS];
    /* Name of BIOS image file */
    char filename[1024];
} flash_t;

typedef struct isp_hdr_t {
    uint8_t entry_type;
    uint8_t entry_count;
    uint8_t seqno;
    uint8_t flags;
} isp_hdr_t;

typedef struct isp_data_seg_t {
    uint32_t address;
    uint32_t length;
} isp_data_seg_t;

/* RQSTYPE_RESPONSE HW structure */
typedef struct isp_req_status_t {
    isp_hdr_t hdr;
    uint32_t handle;
    uint16_t scsi_status;
    uint16_t comp_status;
    uint16_t state_flags;
    uint16_t status_flags;
    uint16_t time;
    uint16_t sense_length;
    uint32_t residual_length;
    uint8_t response[8];
    uint8_t sense_data[32];
} isp_req_status_t;

typedef enum SxpState {
    SXP_STATE_IDLE,
    SXP_STATE_BAD_PACKET,
    SXP_STATE_SELECT_DEVICE,
    SXP_STATE_SELECTION_TIMEOUT,
    SXP_STATE_SEND_CDB,
    SXP_STATE_COMPLETE_COMMAND,
    SXP_STATE_SCHEDULE_MBOX_INTERRUPT,
    SXP_STATE_ACQUIRE_MBOX_SEMAPHORE,
    SXP_STATE_SCHEDULE_RISC_INTERRUPT,
    SXP_STATE_SET_RISC_INTERRUPT,
    SXP_STATE_SCHEDULE_TIMER_SILENCE,
    SXP_STATE_MBOX_WAIT_TIMER,
    SXP_STATE_WAIT_TIMER,
} SxpState;

/* Common structure for request entries (RQSTYPE_REQUEST, RQSTYPE_REQUEST_A64, RQSTYPE_MARKER) */
typedef struct ql_sxp_req_t {
    isp_hdr_t hdr;
    uint32_t handle;
    uint8_t lun;
    uint8_t bus_target;
    uint16_t cdb_length;
    uint16_t flags;
    uint16_t reserved;
    uint16_t timeout;
    uint16_t seg_count;
    uint8_t cdb[12];
} ql_sxp_req_t;

typedef struct ql_fw_target_params {
    uint16_t flags;
    uint16_t sync_period;
} ql_fw_target_params;

typedef struct ql_t {
    /* Hardware registers */
    uint16_t reg_cdma_cfg;
    uint16_t reg_cdma_ctrl;
    uint16_t reg_ddma_cfg;
    uint16_t reg_ddma_ctrl;
    uint16_t reg_risc_psr;
    uint16_t reg_risc_pcr;
    uint16_t reg_risc_pc;
    uint16_t reg_risc_mtr;
    uint16_t reg_id_low;
    uint16_t reg_id_high;
    uint16_t reg_cfg0;
    uint16_t reg_scsi_diff_pins;
    uint16_t reg_gpio_data;
    uint16_t reg_gpio_enable;
    uint16_t reg_flash_bios_addr;
    uint16_t reg_nvram;
    uint16_t reg_cfg1;
    uint16_t reg_host_cmd_flags;
    uint16_t reg_intr_ctrl;
    uint16_t reg_intr_status;
    uint16_t reg_semaphore;
    uint16_t reg_mbox_in[QL_MBOX_REGS_MAX];
    uint16_t reg_mbox_out[QL_MBOX_REGS_MAX];
    /* SCSI executive processor state */
    SxpState sxp_state;
    /* SXP flags */
    uint32_t sxp_flags;
    /* Physical address of the request ring */
    uint32_t rqst_ring_base;
    /* Request ring size */
    uint16_t rqst_ring_size;
    /* Response ring size */
    uint16_t resp_ring_size;
    /* Physical address of the response ring */
    uint32_t resp_ring_base;
    /* Physical address of the current request IOCB */
    uint32_t pkt_address;
    /* Current request IOCB */
    ql_sxp_req_t pkt;
    /* Current response IOCB */
    isp_req_status_t pkt_resp;
    /* Holds the request queue indexes to abort */
    Fifo8 abort_iocbs_fifo;
    /* Current Path ID */
    uint8_t curr_path_id;
    /* Current Target ID */
    uint8_t curr_target_id;
    /* Number of SCSI buses on this HBA */
    uint8_t max_bus_count;
    /* SCSI bus emulation internal index */
    uint8_t scsi_bus;
    /* ISP chip type */
    uint8_t isp_type;
    /* ISP chip revision */
    uint8_t isp_rev;
    /* IRQ emulation internal context */
    uint8_t irq_state;
    /* PCI slot emulation internal context */
    uint8_t pci_slot;
    /* Bit map of mailbox registers to return to the host */
    uint32_t mbox_out_mask;
    /* Pending mailbox response */
    uint16_t mbox_data[QL_MBOX_REGS_MAX];
    /* Transfer speed in bytes per second */
    double xfer_rate_bps;
    /* Command timer period in microsecounds */
    double timer_period;
    /* Command timer */
    pc_timer_t cmd_timer;
    /* Firmware device parameters */
    ql_fw_target_params fw_tid_params[2][QL_MAX_TID];
    /* Firmware retry count and delay settings */
    uint16_t fw_retry_params[4];
    /* Firmware version */
    uint32_t fw_version;
    /* Size of the SCSI payload data */
    uint32_t scsi_data_size;
    /* Current offset in the SCSI payload data */
    uint32_t scsi_data_offset;
    /* Temporary buffer to hold payload data of the SCSI command */
    uint8_t *scsi_data_buffer;
    /* PCI MMIO BAR mapping */
    mem_mapping_t mmio_bar_mapping;
    /* PCI ROM BAR mapping */
    mem_mapping_t rom_bar_mapping;
    /* PCI configuration space */
    uint8_t pci_cfg[256];
    /* Size of the area to map the expansion ROM */
    uint32_t pci_rom_area_size;
    /* Writable bits of the ROM BAR */
    uint32_t rom_bar_mask;
    /* Supports PCI capabilities */
    bool has_pci_caps;
    /* NVRAM device */
    nmc93cxx_eeprom_t *eeprom_device;
    /* Flash BIOS device */
    flash_t flash_device;
    /* RISC CPU memory */
    uint16_t cpu_mem[0x10000];
} ql_t;

extern double cpuclock;

static bool
ql_sxp_fetch_request(ql_sxp_req_t* pkt, uint32_t address);

#ifdef ENABLE_QL_LOG
int ql_do_log = ENABLE_QL_LOG;

static bool ql_fw_load_in_progress = false;
static bool ql_nvram_read_in_progress = false;

static void
ql_log(const char *fmt, ...)
{
    va_list ap;

    if (ql_fw_load_in_progress || ql_nvram_read_in_progress) {
        return;
    }

    if (ql_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

static void
ql_debug_check_for_nvram_access(uint32_t addr)
{
    /* Suppress log messages during NVRAM access */
    if (!ql_nvram_read_in_progress) {
        if (addr == QL_REG_NVRAM) {
            ql_log("QL: Disable NVRAM log\n");
            ql_nvram_read_in_progress = true;
        }
    } else {
        if (addr != QL_REG_NVRAM) {
            ql_nvram_read_in_progress = false;
            ql_log("QL: Enable NVRAM log\n");
        }
    }
}

#define MAKE_CASE(x) case x: return #x;
static char *
ql_debug_cmd_to_name(uint16_t opcode)
{
    switch (opcode) {
        MAKE_CASE(QL_CMD_NOP                      );
        MAKE_CASE(QL_CMD_LOAD_RAM                 );
        MAKE_CASE(QL_CMD_EXEC_FIRMWARE            );
        MAKE_CASE(QL_CMD_DUMP_RAM                 );
        MAKE_CASE(QL_CMD_WRITE_RAM_WORD           );
        MAKE_CASE(QL_CMD_READ_RAM_WORD            );
        MAKE_CASE(QL_CMD_REGISTER_TEST            );
        MAKE_CASE(QL_CMD_VERIFY_CHECKSUM          );
        MAKE_CASE(QL_CMD_ABOUT_FIRMWARE           );
        MAKE_CASE(QL_CMD_LOAD_RAM_A64             );
        MAKE_CASE(QL_CMD_DUMP_RAM_A64             );
        MAKE_CASE(QL_CMD_INIT_REQ_QUEUE           );
        MAKE_CASE(QL_CMD_INIT_RSP_QUEUE           );
        MAKE_CASE(QL_CMD_EXECUTE_IOCB             );
        MAKE_CASE(QL_CMD_ABORT_COMMAND            );
        MAKE_CASE(QL_CMD_ABORT_DEVICE             );
        MAKE_CASE(QL_CMD_ABORT_TARGET             );
        MAKE_CASE(QL_CMD_BUS_RESET                );
        MAKE_CASE(QL_CMD_START_QUEUE              );
        MAKE_CASE(QL_CMD_GET_FIRMWARE_STATUS      );
        MAKE_CASE(QL_CMD_GET_RETRY_COUNT          );
        MAKE_CASE(QL_CMD_GET_ACT_NEG_STATE        );
        MAKE_CASE(QL_CMD_GET_TARGET_PARAMETERS    );
        MAKE_CASE(QL_CMD_SET_INITIATOR_ID         );
        MAKE_CASE(QL_CMD_SET_SELECTION_TIMEOUT    );
        MAKE_CASE(QL_CMD_SET_RETRY_COUNT          );
        MAKE_CASE(QL_CMD_SET_TAG_AGE_LIMIT        );
        MAKE_CASE(QL_CMD_SET_CLOCK_RATE           );
        MAKE_CASE(QL_CMD_SET_ACTIVE_NEGATION      );
        MAKE_CASE(QL_CMD_SET_ASYNC_DATA_SETUP     );
        MAKE_CASE(QL_CMD_SET_PCI_CONTROL          );
        MAKE_CASE(QL_CMD_SET_TARGET_PARAMETERS    );
        MAKE_CASE(QL_CMD_SET_DEVICE_QUEUE         );
        MAKE_CASE(QL_CMD_RETURN_BIOS_BLOCK_ADDR   );
        MAKE_CASE(QL_CMD_WRITE_FOUR_RAM_WORDS     );
        MAKE_CASE(QL_CMD_EXEC_BIOS_IOCB           );
        MAKE_CASE(QL_CMD_SET_SYSTEM_PARAMETER     );
        MAKE_CASE(QL_CMD_SET_FIRMWARE_FEATURES    );
        MAKE_CASE(QL_CMD_INIT_REQ_QUEUE_A64       );
        MAKE_CASE(QL_CMD_INIT_RSP_QUEUE_A64       );
        MAKE_CASE(QL_CMD_EXECUTE_IOCB_A64         );
        MAKE_CASE(QL_CMD_GET_TRANSFER_MODE        );
        MAKE_CASE(QL_CMD_SET_DATA_OVERRUN_RECOVERY);
        default:
            return "<UNK>";
    }
}

static char *
debug_sxp_state_to_name(SxpState state)
{
    switch (state) {
        MAKE_CASE(SXP_STATE_IDLE);
        MAKE_CASE(SXP_STATE_BAD_PACKET);
        MAKE_CASE(SXP_STATE_SELECT_DEVICE);
        MAKE_CASE(SXP_STATE_SELECTION_TIMEOUT);
        MAKE_CASE(SXP_STATE_SEND_CDB);
        MAKE_CASE(SXP_STATE_COMPLETE_COMMAND);
        MAKE_CASE(SXP_STATE_SCHEDULE_MBOX_INTERRUPT);
        MAKE_CASE(SXP_STATE_SCHEDULE_RISC_INTERRUPT);
        MAKE_CASE(SXP_STATE_SET_RISC_INTERRUPT);
        MAKE_CASE(SXP_STATE_ACQUIRE_MBOX_SEMAPHORE);
        MAKE_CASE(SXP_STATE_SCHEDULE_TIMER_SILENCE);
        MAKE_CASE(SXP_STATE_WAIT_TIMER);
        MAKE_CASE(SXP_STATE_MBOX_WAIT_TIMER);
        default:
            return "<UNK>";
    }
}
#undef MAKE_CASE
#else
#    define ql_log(fmt, ...)
#endif

static flash_block_t*
am29_address_to_block(flash_t *dev, uint32_t addr)
{
    for (uint32_t i = 0; i < AM29_MAX_BLOCKS; i++) {
        flash_block_t *block = &dev->block[i];

        if ((addr >= block->start_addr) && (addr <= block->end_addr))
            return block;
    }

    /* Should not happen */
    assert(false);
    return NULL;
}

static void
am29_reset_cmd_sequence(flash_t *dev)
{
    dev->bus_cycle = 0;
    dev->cmd_cycle = 0;
}

static void
am29_set_mode(flash_t *dev, FlashMode mode)
{
    if (mode != dev->mode) {
        ql_log("FLASH: Set mode %u\n", mode);
    }
    dev->mode = mode;

    am29_reset_cmd_sequence(dev);
}

static void
am29_process_reset_or_complete_cmd(flash_t *dev)
{
    am29_set_mode(dev, M_READ_ARRAY);

    dev->status_reg = 0;
    dev->blocks_to_erase_bitmap = 0;

    /* Terminate the block erase timeout */
    timer_stop(&dev->erase_accept_timeout_timer);
}

static void
am29_reset(flash_t *dev)
{
    am29_process_reset_or_complete_cmd(dev);
}

static void
am29_cmd_complete_timer_callback(void *priv)
{
    flash_t *dev = priv;

    ql_log("FLASH: Command completed with status %02X\n", dev->status_reg);

    /* The memory will return to the Read Mode, unless an error has occurred */
    if (dev->status_reg & FLASH_STATUS_ERROR) {
        return;
    }
    am29_process_reset_or_complete_cmd(dev);
}

static void
am29_erase_blocks(flash_t *dev)
{
    for (uint32_t i = 0; i < AM29_MAX_BLOCKS; i++) {
        flash_block_t *block = &dev->block[i];
        void *block_start;
        size_t block_length;

        if (!(dev->blocks_to_erase_bitmap & (1 << i))) {
            continue;
        }

        /* Protected block: The data remains unchanged, no error is given */
        if (block->protection_status == FLASH_BLOCK_STATUS_PROTECTED) {
            continue;
        }

        ql_log("FLASH: Erase block #%lu %lX-%lX\n", block->number, block->start_addr, block->end_addr);

        block_start = &dev->array_data[block->start_addr];
        block_length = (block->end_addr - block->start_addr) + 1;

        memset(block_start, 0xFF, block_length);
    }
}

static void
am29_erase_begin_timer_callback(void *priv)
{
    flash_t *dev = priv;

    ql_log("FLASH: Begin erase operation\n");

    /* Finally, erase the blocks (fill with 0xFF) */
    am29_erase_blocks(dev);

    if (dev->mode == M_CHIP_ERASE) {
        timer_on_auto(&dev->cmd_complete_timer, dev->chip_erase_time_us);
    } else {
        dev->status_reg |= FLASH_STATUS_ERASE_TIMEOUT_EXPIRED;

        timer_on_auto(&dev->cmd_complete_timer, dev->block_erase_time_us);
    }
}

static uint8_t
am29_status_register_read(flash_t *dev)
{
    switch (dev->mode) {
        case M_PROGRAM:
        case M_BLOCK_ERASE:
        case M_CHIP_ERASE: {
            dev->status_reg ^= FLASH_STATUS_TOGGLE;
            break;
        }

        default:
            break;
    }

    return dev->status_reg;
}

static void
am29_accept_cmd(flash_t *dev, uint32_t addr, uint8_t val)
{
    flash_block_t *block;

    /* Single cycle command (write to any address inside the device) */
    if ((val == FLASH_CMD_READ_ARRAY) && (dev->mode != M_PROGRAM)) {
        am29_process_reset_or_complete_cmd(dev);
        return;
    }

    switch (dev->mode) {
        case M_PROGRAM: {
            block = am29_address_to_block(dev, addr);

            /* Write to a protected block: The data remains unchanged, no error is given */
            if (block->protection_status == FLASH_BLOCK_STATUS_PROTECTED) {
                ql_log("FLASH: Program failure - the block #lu is protected\n", block->number);
                goto ProgramDone;
            }

            /* The program command cannot change a bit set at '0' back to '1' */
            if (~dev->array_data[addr] & val) {
                ql_log("FLASH: Program error - the address %lX "
                       "was not previously erased %04X <> %04X\n",
                       addr,
                       dev->array_data[addr],
                       val);

                dev->status_reg |= FLASH_STATUS_ERROR;
                goto ProgramDone;
            }

            /* Data# polling */
            if (!(val & FLASH_STATUS_DATA_POLLING)) {
                dev->status_reg |= FLASH_STATUS_DATA_POLLING;
            }

            /* Finally, program the value */
            ql_log("FLASH: Program %08lX to %04X\n", addr, val);
            dev->array_data[addr] = val;

ProgramDone:
            timer_on_auto(&dev->cmd_complete_timer, dev->program_time_us);
            break;
        }

        case M_BLOCK_ERASE: {
            /* We shouldn't get here if the operation has already started */
            assert(!(dev->status_reg & FLASH_STATUS_ERASE_TIMEOUT_EXPIRED));

            addr &= dev->block_select_addr_mask;
            block = am29_address_to_block(dev, addr);

            ql_log("FLASH: Queued block #%lu %lX-%lX for erase\n",
                   block->number,
                   block->start_addr,
                   block->end_addr);

            /* Add block to the list */
            dev->blocks_to_erase_bitmap |= 1 << block->number;

            /* Wait for a next block to erase (restart the timeout period) */
            timer_stop(&dev->erase_accept_timeout_timer);
            timer_on_auto(&dev->erase_accept_timeout_timer, AM29_BLOCK_ERASE_ACCEPT_TIMEOUT_US);
            break;
        }

        case M_CHIP_ERASE: {
            /* Add all blocks to the list */
            dev->blocks_to_erase_bitmap = 0xFFFFFFFF;

            /* Immediately start the erase operation */
            am29_erase_begin_timer_callback(dev);
            break;
        }

        default:
            break;
    }
}

static void
am29_interpret_cmd_sequence(flash_t *dev, uint32_t addr, uint8_t val)
{
    // clang-format off
    static const uint8_t cmd_seq_next_state[6][2] = {
        //    Phase 0                 Phase 1
        { CYCLE_CHECK_AA,         CYCLE_INVALID       }, // Cycle 1
        { CYCLE_CHECK_55,         CYCLE_INVALID       }, // Cycle 2
        { CYCLE_CHECK_FIRST_CMD,  CYCLE_INVALID       }, // Cycle 3
        { CYCLE_CHECK_AA,         CYCLE_ENTER_PROGRAM }, // Cycle 4
        { CYCLE_CHECK_55,         CYCLE_INVALID       }, // Cycle 5
        { CYCLE_CHECK_SECOND_CMD, CYCLE_INVALID       }, // Cycle 6
    };
    // clang-format on

    addr &= dev->cmd_cycle_addr_mask;

    switch (cmd_seq_next_state[dev->bus_cycle][dev->cmd_cycle]) {
        case CYCLE_CHECK_AA: {
            if ((val == 0xAA) && (addr == dev->addr_5555_phys)) {
                dev->bus_cycle++;
                return;
            }
            break;
        }

        case CYCLE_CHECK_55: {
            if ((val == 0x55) && (addr == dev->addr_AAAA_phys)) {
                dev->bus_cycle++;
                return;
            }
            break;
        }

        case CYCLE_CHECK_FIRST_CMD: {
            if (addr != dev->addr_5555_phys) {
                break;
            }

            switch (val) {
                case FLASH_CMD_AUTO_SELECT:
                    am29_set_mode(dev, M_AUTO_SELECT);
                    return;

                case FLASH_CMD_PROGRAM:
                    dev->bus_cycle++;
                    dev->cmd_cycle++;
                    return;

                case FLASH_CMD_SETUP_ERASE:
                    dev->bus_cycle++;
                    return;

                default:
                    break;
            }
            break;
        }

        case CYCLE_ENTER_PROGRAM: {
            am29_set_mode(dev, M_PROGRAM);
            return;
        }

        case CYCLE_CHECK_SECOND_CMD: {
            switch (val) {
                case FLASH_CMD_BLOCK_ERASE_CONFIRM: {
                    am29_set_mode(dev, M_BLOCK_ERASE);
                    return;
                }

                case FLASH_CMD_CHIP_ERASE_CONFIRM: {
                    if (addr == dev->addr_5555_phys) {
                        am29_set_mode(dev, M_CHIP_ERASE);
                        return;
                    }
                    break;
                }

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    am29_reset_cmd_sequence(dev);
}

static void
am29_mmio_write8(uint32_t addr, uint8_t val, flash_t *dev)
{
    cycles -= dev->access_cycles;

    addr &= (AM29_FLASH_SIZE - 1);

    ql_log("FLASH: [%08lX] <-- %02X\n", addr, val);

    switch (dev->mode) {
        /* Ignore all commands while the chip is being programmed or erased */
        case M_CHIP_ERASE:
        case M_PROGRAM: {
            /* A Read/Reset command can be issued to reset the error condition */
            if ((dev->status_reg & FLASH_STATUS_ERROR) && (val == FLASH_CMD_READ_ARRAY))
                break;
            return;
        }

        /* Ignore all commands during the Block Erase except the Read/Reset command */
        case M_BLOCK_ERASE: {
            /* The command has not started yet, we keep accepting blocks to the erase list */
            if (!(dev->status_reg & FLASH_STATUS_ERASE_TIMEOUT_EXPIRED))
                break;

            if (val == FLASH_CMD_READ_ARRAY)
                break;
            return;
        }

        default:
            break;
    }

    /* Receive the command sequence */
    if ((dev->mode == M_READ_ARRAY) || (dev->mode == M_AUTO_SELECT)) {
        am29_interpret_cmd_sequence(dev, addr, val);
    }

    /* Begin the operation */
    am29_accept_cmd(dev, addr, val);
}

static uint8_t
am29_mmio_read8(uint32_t addr, flash_t *dev)
{
    uint8_t ret;

    cycles -= dev->access_cycles;

    addr &= (AM29_FLASH_SIZE - 1);

    switch (dev->mode) {
        case M_READ_ARRAY: {
            /* Read array data */
            ret = dev->array_data[addr];
            break;
        }

        case M_AUTO_SELECT: {
            switch (addr & AM29_AUTOSEL_ADDR_MASK) {
                case AM29_AUTOSEL_ADDR_MANUFACTURER_ID:
                    ret = dev->manufacturer_id;
                    break;

                case AM29_AUTOSEL_ADDR_MODEL_ID:
                    ret = dev->model_id;
                    break;

                default: {
                    flash_block_t *block = am29_address_to_block(dev, addr);

                    /* Read the block protection status  */
                    ret = block->protection_status;
                    break;
                }
            }
            break;
        }

        default: {
            /* Return the status register during Program and Erase operations */
            ret = am29_status_register_read(dev);
            break;
        }
    }

    if (dev->mode != M_READ_ARRAY) {
        ql_log("FLASH: [%08lX] --> %02X\n", addr, ret);
    }
    return ret;
}

static uint8_t
ql_rom_bar_mmio_read8(uint32_t addr, void* priv)
{
    ql_t *dev = priv;

    return am29_mmio_read8(addr, &dev->flash_device);
}

static void
ql_update_irq(ql_t *dev)
{
    uint16_t isr = dev->reg_intr_status & dev->reg_intr_ctrl;

    /* NOTE: Unmasking QL_INTR_REQ does nothing on QLA1080 real hardware */
    isr &= QL_RISC_INTR_REQ;

    if (isr != 0)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

static void
ql_reset_asic(ql_t *dev)
{
    ql_log("QL: Reset ASIC\n");

    dev->reg_id_low = 0x1077;
    dev->reg_id_high = 0x1240;
    dev->reg_cfg0 = dev->isp_rev;
    dev->reg_cfg1 = BIU_BURST_ENABLE | BIU_PCI_CONF1_FIFO_128;
    dev->reg_intr_ctrl = 0;
    dev->reg_intr_status = 0;
    dev->reg_semaphore = 0;
    dev->reg_nvram = 0;
    dev->reg_flash_bios_addr = 0;
    dev->reg_mbox_out[0] = 0;
    dev->reg_mbox_out[1] = 0x4953; // 'IS'
    dev->reg_mbox_out[2] = 0x5020; // 'P '
    dev->reg_mbox_out[3] = 0x2020; // '  '
    dev->reg_mbox_out[4] = 1;
    dev->reg_mbox_out[5] = 7;
    dev->reg_mbox_out[6] = 0;
    dev->reg_mbox_out[7] = 1;
    dev->reg_host_cmd_flags = 0;
    dev->reg_gpio_data = 0x000F;
    dev->reg_gpio_enable = 0;
    dev->reg_scsi_diff_pins = SXP_PINS_SE_MODE;
    dev->reg_cdma_cfg = 0x0001;
    dev->reg_cdma_ctrl = 0;
    dev->reg_ddma_cfg = 0x0008;
    dev->reg_ddma_ctrl = 0;
    dev->reg_risc_psr = 0x2000;
    dev->reg_risc_pcr = 0;
    dev->reg_risc_pc = 0;
    dev->reg_risc_mtr = 0x0001;

    memset(dev->cpu_mem, 0, sizeof(dev->cpu_mem));
    /* FastUtil BIOS checks area 0xFF03-0xFFF3 if the word at 0xFF03 is invalid */
    dev->cpu_mem[0xFF03] = 0xFBFF;

    timer_stop(&dev->cmd_timer);
    dev->rqst_ring_base = 0;
    dev->resp_ring_base = 0;
    dev->sxp_flags = 0;
    dev->sxp_state = SXP_STATE_IDLE;

    ql_update_irq(dev);
}

static bool
ql_sxp_abort_commands(ql_t *dev, uint8_t path_id, uint8_t target_id, uint8_t lun, uint32_t handle, bool is_handle_valid)
{
    bool success = false;
    uint16_t cons;

    if (!(dev->sxp_flags & SXP_FLAG_ENGINE_ACTIVE)) {
        return false;
    }

    /* Iterate over the request IOCBs looking for a match */
    for (cons = QL_RQST_CONS(dev); cons != QL_RQST_PROD(dev); cons = (cons + 1) % dev->rqst_ring_size) {
        uint32_t pkt_address = dev->rqst_ring_base + cons * QENTRY_LEN;
        ql_sxp_req_t pkt;
        uint8_t curr_path_id, curr_target_id;

        if (!ql_sxp_fetch_request(&pkt, pkt_address)) {
            continue;
        }

        curr_path_id = (pkt.bus_target & 0x80) >> 7;
        curr_target_id = pkt.bus_target & ~0x80;

        /* Exact match? */
        if ((curr_path_id != path_id) || (curr_target_id != target_id)) {
            continue;
        }
        if ((lun != 0xFF) && (pkt.lun != lun)) {
            continue;
        }
        if (is_handle_valid && (pkt.handle != handle)) {
            continue;
        }

        /* Save the queue index, it will be checked before the next command is processed */
        fifo8_push(&dev->abort_iocbs_fifo, cons & 0xFF);
        fifo8_push(&dev->abort_iocbs_fifo, (cons >> 8) & 0xFF);

        success = true;
    }

    return success;
}

static void
ql_sxp_initialize_queues(ql_t *dev)
{
    if ((dev->rqst_ring_base != 0) && (dev->resp_ring_base != 0)) {
        /* Each queue index consists of 2 bytes (size of the mailbox register) */
        uint32_t fifo_size = dev->rqst_ring_size * sizeof(uint16_t);

        fifo8_destroy(&dev->abort_iocbs_fifo);
        fifo8_create(&dev->abort_iocbs_fifo, fifo_size);

        dev->sxp_flags |= SXP_FLAG_ENGINE_ACTIVE;
    } else {
        dev->sxp_flags &= ~SXP_FLAG_ENGINE_ACTIVE;
    }
}

static uint16_t
ql_handle_cmd_nop(ql_t *dev)
{
    /* Nothing to do */
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_write_ram_word(ql_t *dev)
{
#ifdef ENABLE_QL_LOG
    /* Suppress log messages during loading of firmware */
    if (!ql_fw_load_in_progress) {
        if (dev->reg_mbox_in[1] == 0x1003) {
            ql_log("QL: Disable log before FW load\n");
            ql_fw_load_in_progress = true;
        }
    } else {
        if ((dev->reg_mbox_in[1] - 0x1000) == (dev->cpu_mem[0x1003] - 2)) {
            ql_fw_load_in_progress = false;
            ql_log("QL: Enable log after FW load\n");
        }
    }
#endif

    dev->cpu_mem[dev->reg_mbox_in[1]] = dev->reg_mbox_in[2];

    dev->mbox_out_mask |= QL_BIT(2);
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_write_four_ram_words(ql_t *dev)
{
    uint16_t address = dev->reg_mbox_in[1];

    if (dev->isp_type != QL_ISP1040) {
        return QL_MBOX_STATUS_INVALID;
    }

    if (address >= (ARRAY_SIZE(dev->cpu_mem) - 4)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    dev->cpu_mem[address + 0] = dev->reg_mbox_in[2];
    dev->cpu_mem[address + 1] = dev->reg_mbox_in[3];
    dev->cpu_mem[address + 2] = dev->reg_mbox_in[4];
    dev->cpu_mem[address + 3] = dev->reg_mbox_in[5];

    if (address == (QL_IOCB_FW_BASE + 4)) {
        /* Save the SCSI device address for later use */
        dev->cpu_mem[QL_IOCB_FW_BASE + 48] = dev->cpu_mem[address];

        /* Return some status data */
        dev->cpu_mem[address] = 0x0300;
    }

    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_read_ram_word(ql_t *dev)
{
    dev->mbox_out_mask |= QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[2] = dev->cpu_mem[dev->reg_mbox_in[1]];
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_load_ram_block(ql_t *dev, UNUSED(bool is_64bit_addr))
{
    uint32_t offset = dev->reg_mbox_in[1];
    uint32_t block_size_words = dev->reg_mbox_in[4];
    uint8_t *dest_address = (uint8_t *)&dev->cpu_mem[offset];
    uint32_t src_address = ((uint32_t)dev->reg_mbox_in[2] << 16) | dev->reg_mbox_in[3];

    if ((offset + block_size_words) > ARRAY_SIZE(dev->cpu_mem)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    ql_dma_read(src_address, dest_address, block_size_words * sizeof(uint16_t));
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_dump_ram_block(ql_t *dev, UNUSED(bool is_64bit_addr))
{
    uint32_t offset = dev->reg_mbox_in[1];
    uint32_t block_size_words = dev->reg_mbox_in[4];
    uint8_t *src_address = (uint8_t *)&dev->cpu_mem[offset];
    uint32_t dest_address = ((uint32_t)dev->reg_mbox_in[2] << 16) | dev->reg_mbox_in[3];

    if ((offset + block_size_words) > ARRAY_SIZE(dev->cpu_mem)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    ql_dma_write(dest_address, src_address, block_size_words * sizeof(uint16_t));
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_register_test(ql_t *dev)
{
    for (uint32_t i = 1; i < ARRAY_SIZE(dev->reg_mbox_in); i++) {
        dev->mbox_out_mask |= QL_BIT(i);
        dev->mbox_data[i] = dev->reg_mbox_in[i];
    }

    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_verify_checksum(ql_t *dev)
{
    uint32_t start_addr = dev->reg_mbox_in[1];
    uint16_t fw_size_words;
    uint32_t end_addr;
    uint16_t crc;

    if (start_addr >= (ARRAY_SIZE(dev->cpu_mem) - 3)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    fw_size_words = dev->cpu_mem[start_addr + 3];

    end_addr = start_addr + fw_size_words;
    if (end_addr > ARRAY_SIZE(dev->cpu_mem)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    crc = 0;
    for (uint32_t i = start_addr; i < end_addr; i++) {
        crc += dev->cpu_mem[i];
    }
    dev->mbox_out_mask |= QL_BIT(1);
    dev->mbox_data[1] = crc;

    if (crc != 0) {
        ql_log("QL: Firmware crc 0x%X\n", crc);
        return QL_MBOX_STATUS_TEST_FAILED;
    }

    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_exec_firmware(ql_t *dev)
{
    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3) | QL_BIT(4) | QL_BIT(5);
    dev->mbox_data[1] = 0x4953; // 'IS'
    dev->mbox_data[2] = 0x5020; // 'P '
    dev->mbox_data[3] = 0x2020; // '  '
    dev->mbox_data[4] = 8;
    dev->mbox_data[5] = 0x04FE;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_about_firmware(ql_t *dev)
{
    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = (dev->fw_version >> 16) & 0xFF; // Major revision
    dev->mbox_data[2] = (dev->fw_version >> 8) & 0xFF; // Minor revision
    dev->mbox_data[3] = (dev->fw_version >> 0) & 0xFF; // Micro revision
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_init_request_queue(ql_t *dev, UNUSED(bool is_64bit_addr))
{
    uint32_t address = ((uint32_t)dev->reg_mbox_in[2] << 16) | dev->reg_mbox_in[3];
    uint16_t queue_length = dev->reg_mbox_in[1];

    ql_log("QL: REQ queue address 0x%X, length %u\n", address, queue_length);

    if ((address == 0) || (queue_length == 0)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    dev->rqst_ring_base = address;
    dev->rqst_ring_size = queue_length;
    ql_sxp_initialize_queues(dev);

    dev->mbox_out_mask |= 1 << QL_MBOX_RQST;
    dev->mbox_data[QL_MBOX_RQST] = dev->reg_mbox_in[QL_MBOX_RQST];
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_init_response_queue(ql_t *dev, UNUSED(bool is_64bit_addr))
{
    uint32_t address = ((uint32_t)dev->reg_mbox_in[2] << 16) | dev->reg_mbox_in[3];
    uint16_t queue_length = dev->reg_mbox_in[1];

    ql_log("QL: RSP queue address 0x%X, length %u\n", address, queue_length);

    if ((address == 0) || (queue_length == 0)) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    dev->resp_ring_base = address;
    dev->resp_ring_size = queue_length;
    ql_sxp_initialize_queues(dev);

    dev->mbox_out_mask |= 1 << QL_MBOX_RESP;
    dev->mbox_data[QL_MBOX_RESP] = dev->reg_mbox_in[QL_MBOX_RESP];
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_abort_command(ql_t *dev)
{
    uint8_t path_id = dev->reg_mbox_in[1] >> 15;
    uint8_t target_id = (dev->reg_mbox_in[1] >> 8) & ~0x80;
    uint8_t lun = dev->reg_mbox_in[1] & 0xFF;
    uint32_t handle;
    bool success;

    ql_log("QL: [%u:%u] Abort command\n", path_id, target_id);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    if (target_id >= QL_MAX_TID) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    /* Abort an active command on the device (PATH:TID:LUN) that match the handle */
    handle = dev->reg_mbox_in[QL_MBOX_HNDL_LOW];
    handle |= (uint32_t)dev->reg_mbox_in[QL_MBOX_HNDL_HIGH] << 16;
    success = ql_sxp_abort_commands(dev, path_id, target_id, lun, handle, true);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = dev->reg_mbox_in[3];

    if (!success) {
        return QL_MBOX_STATUS_CMD_ERROR;
    }

    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_abort_device(ql_t *dev)
{
    uint8_t path_id = dev->reg_mbox_in[1] >> 15;
    uint8_t target_id = (dev->reg_mbox_in[1] >> 8) & ~0x80;
    uint8_t lun = dev->reg_mbox_in[1] & 0xFF;

    ql_log("QL: [%u:%u:%u] Abort device\n", path_id, target_id, lun);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    if (target_id >= QL_MAX_TID) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    /* Abort all active commands on the device (PATH:TID:LUN) */
    (void) ql_sxp_abort_commands(dev, path_id, target_id, lun, 0, false);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = dev->reg_mbox_in[3];
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_abort_target(ql_t *dev)
{
    uint8_t path_id = dev->reg_mbox_in[1] >> 15;
    uint8_t target_id = (dev->reg_mbox_in[1] >> 8) & ~0x80;

    ql_log("QL: [%u:%u] Abort target\n", path_id, target_id);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    if (target_id >= QL_MAX_TID) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    /* Abort all active commands on the target (PATH:TID) */
    (void) ql_sxp_abort_commands(dev, path_id, target_id, 0xFF, 0, false);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = dev->reg_mbox_in[3];
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_bus_reset(ql_t *dev)
{
    uint8_t path_id;

    if (dev->isp_type == QL_ISP1040) {
        path_id = 0;
    } else {
        path_id = dev->reg_mbox_in[2];
    }

    ql_log("QL: Reset bus %u, delay %u\n", path_id, dev->reg_mbox_in[1]);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    /* 86Box supports one SCSI bus per controller for now */
    if (path_id == 0) {
        for (uint32_t i = 0; i < MIN(QL_MAX_TID, SCSI_ID_MAX); i++) {
            scsi_device_reset(&scsi_devices[dev->scsi_bus][i]);
        }
    }

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_get_firmware_status(ql_t *dev)
{
    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = 0;
    dev->mbox_data[2] = 527; // Max queue depth
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_initialor_id(ql_t *dev)
{
    ql_log("QL: Initialor ID 0x%X\n", dev->reg_mbox_in[1]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_selection_timeout(ql_t *dev)
{
    ql_log("QL: Selection timeout #1 %u\n", dev->reg_mbox_in[1]);
    ql_log("QL: Selection timeout #2 %u\n", dev->reg_mbox_in[2]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_retry_count(ql_t *dev)
{
    ql_log("QL: Retry count #1 %u\n", dev->reg_mbox_in[1]);
    ql_log("QL: Retry count #2 %u\n", dev->reg_mbox_in[2]);
    ql_log("QL: Retry delay #1 %u\n", dev->reg_mbox_in[6]);
    ql_log("QL: Retry delay #2 %u\n", dev->reg_mbox_in[7]);

    dev->fw_retry_params[0] = dev->reg_mbox_in[1];
    dev->fw_retry_params[1] = dev->reg_mbox_in[2];
    dev->fw_retry_params[2] = dev->reg_mbox_in[6];
    dev->fw_retry_params[3] = dev->reg_mbox_in[7];

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = 8;
    dev->mbox_data[2] = 5;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_get_retry_count(ql_t *dev)
{
    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->fw_retry_params[0];
    dev->mbox_data[2] = dev->fw_retry_params[1];
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_get_act_neg_state(ql_t *dev)
{
    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = 0;
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_tag_age_limit(ql_t *dev)
{
    ql_log("QL: Tag age limit #1 %u\n", dev->reg_mbox_in[1]);
    ql_log("QL: Tag age limit #2 %u\n", dev->reg_mbox_in[2]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_clock_rate(ql_t *dev)
{
    ql_log("QL: Clock rate %u\n", dev->reg_mbox_in[1]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_active_negation(ql_t *dev)
{
    ql_log("QL: Acq active negation 0x%X\n", dev->reg_mbox_in[1]);
    ql_log("QL: Data line active negation 0x%X\n", dev->reg_mbox_in[2]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_async_data_setup(ql_t *dev)
{
    ql_log("QL: Async data setup %u %u\n", dev->reg_mbox_in[1], dev->reg_mbox_in[2]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_pci_control(ql_t *dev)
{
    ql_log("QL: PCI control %x %x\n", dev->reg_mbox_in[1], dev->reg_mbox_in[2]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_target_parameters(ql_t *dev)
{
    uint8_t path_id = dev->reg_mbox_in[1] >> 15;
    uint8_t target_id = (dev->reg_mbox_in[1] >> 8) & ~0x80;

    ql_log("QL: [%u:%u] Set params 0x%X 0x%X\n", path_id, target_id, dev->reg_mbox_in[2], dev->reg_mbox_in[3]);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    if (target_id >= QL_MAX_TID) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    dev->fw_tid_params[path_id][target_id].flags = dev->reg_mbox_in[2];
    dev->fw_tid_params[path_id][target_id].sync_period = dev->reg_mbox_in[3];

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = dev->reg_mbox_in[3];
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_get_target_parameters(ql_t *dev)
{
    uint8_t path_id = dev->reg_mbox_in[1] >> 15;
    uint8_t target_id = (dev->reg_mbox_in[1] >> 8) & ~0x80;

    ql_log("QL: [%u:%u] Return params\n", path_id, target_id);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    if (target_id >= QL_MAX_TID) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3) | QL_BIT(6);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->fw_tid_params[path_id][target_id].flags;
    dev->mbox_data[3] = dev->fw_tid_params[path_id][target_id].sync_period;
    dev->mbox_data[6] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_device_queue(ql_t *dev)
{
    ql_log("QL: Queue params 0x%X %u %u\n", dev->reg_mbox_in[1], dev->reg_mbox_in[2], dev->reg_mbox_in[3]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = dev->reg_mbox_in[2];
    dev->mbox_data[3] = 0x0064;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_return_bios_block_addr(ql_t *dev)
{
    if (dev->isp_type != QL_ISP1040) {
        return QL_MBOX_STATUS_INVALID;
    }

    dev->mbox_out_mask |= QL_BIT(1);
    dev->mbox_data[1] = QL_IOCB_FW_BASE;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_system_parameter(ql_t *dev)
{
    ql_log("QL: System parameter 0x%X\n", dev->reg_mbox_in[1]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = 2;
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_firmware_features(ql_t *dev)
{
    ql_log("QL: FW features 0x%X\n", dev->reg_mbox_in[1]);

    if (dev->reg_mbox_in[1] & FW_FEATURE_FAST_POST) {
        dev->sxp_flags |= SXP_FLAG_FAST_POSTING;
    } else {
        dev->sxp_flags &= ~SXP_FLAG_FAST_POSTING;
    }

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = 0;
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_get_transfer_mode(ql_t *dev)
{
    uint8_t path_id = dev->reg_mbox_in[1] >> 15;
    uint8_t target_id = (dev->reg_mbox_in[1] >> 8) & ~0x80;

    ql_log("QL: [%u:%u] Return xfer mode\n", path_id, target_id);

    if (path_id >= dev->max_bus_count) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    if (target_id >= QL_MAX_TID) {
        return QL_MBOX_STATUS_CMD_PARAM_ERROR;
    }

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3) | QL_BIT(6);
    dev->mbox_data[1] = dev->reg_mbox_in[1];
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0x000C;
    dev->mbox_data[6] = 0xFD10;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_cmd_set_data_overrun_recovery(ql_t *dev)
{
    ql_log("QL: Data overrun recovery 0x%X\n", dev->reg_mbox_in[1]);

    dev->mbox_out_mask |= QL_BIT(1) | QL_BIT(2) | QL_BIT(3);
    dev->mbox_data[1] = 0;
    dev->mbox_data[2] = 0;
    dev->mbox_data[3] = 0;
    return QL_MBOX_STATUS_COMPLETE;
}

static uint16_t
ql_handle_exec_bios_iocb(ql_t *dev)
{
    if (dev->isp_type != QL_ISP1040) {
        return QL_MBOX_STATUS_INVALID;
    }

    dev->sxp_flags |= SXP_FLAG_BIOS_IOCB;
    return QL_MBOX_STATUS_PENDING;
}

static uint16_t
ql_handle_exec_mbox_iocb(ql_t *dev, bool is_64bit_addr)
{
    if (is_64bit_addr && (dev->reg_mbox_in[1] != QENTRY_LEN)) {
        return QL_MBOX_STATUS_CMD_ERROR;
    }

    dev->sxp_flags |= SXP_FLAG_MBOX_IOCB;
    return QL_MBOX_STATUS_PENDING;
}

static uint16_t
ql_process_mailbox(ql_t *dev)
{
    uint16_t status;

    ql_log("QL: Command %02X %s\n", dev->reg_mbox_in[0], ql_debug_cmd_to_name(dev->reg_mbox_in[0]));

    switch (dev->reg_mbox_in[0]) {
        case QL_CMD_NOP:
            status = ql_handle_cmd_nop(dev);
            break;
        case QL_CMD_LOAD_RAM:
            status = ql_handle_cmd_load_ram_block(dev, false);
            break;
        case QL_CMD_EXEC_FIRMWARE:
            status = ql_handle_cmd_exec_firmware(dev);
            break;
        case QL_CMD_DUMP_RAM:
            status = ql_handle_cmd_dump_ram_block(dev, false);
            break;
        case QL_CMD_WRITE_RAM_WORD:
            status = ql_handle_cmd_write_ram_word(dev);
            break;
        case QL_CMD_READ_RAM_WORD:
            status = ql_handle_cmd_read_ram_word(dev);
            break;
        case QL_CMD_REGISTER_TEST:
            status = ql_handle_cmd_register_test(dev);
            break;
        case QL_CMD_VERIFY_CHECKSUM:
            status = ql_handle_cmd_verify_checksum(dev);
            break;
        case QL_CMD_ABOUT_FIRMWARE:
            status = ql_handle_cmd_about_firmware(dev);
            break;
        case QL_CMD_LOAD_RAM_A64:
            status = ql_handle_cmd_load_ram_block(dev, true);
            break;
        case QL_CMD_DUMP_RAM_A64:
            status = ql_handle_cmd_dump_ram_block(dev, true);
            break;
        case QL_CMD_INIT_REQ_QUEUE:
            status = ql_handle_cmd_init_request_queue(dev, false);
            break;
        case QL_CMD_INIT_RSP_QUEUE:
            status = ql_handle_cmd_init_response_queue(dev, false);
            break;
        case QL_CMD_EXECUTE_IOCB:
            status = ql_handle_exec_mbox_iocb(dev, false);
            break;
        case QL_CMD_ABORT_COMMAND:
            status = ql_handle_cmd_abort_command(dev);
            break;
        case QL_CMD_ABORT_DEVICE:
            status = ql_handle_cmd_abort_device(dev);
            break;
        case QL_CMD_ABORT_TARGET:
            status = ql_handle_cmd_abort_target(dev);
            break;
        case QL_CMD_BUS_RESET:
            status = ql_handle_cmd_bus_reset(dev);
            break;
        case QL_CMD_START_QUEUE:
            status = ql_handle_cmd_nop(dev);
            break;
        case QL_CMD_GET_RETRY_COUNT:
            status = ql_handle_cmd_get_retry_count(dev);
            break;
        case QL_CMD_GET_ACT_NEG_STATE:
            status = ql_handle_cmd_get_act_neg_state(dev);
            break;
        case QL_CMD_GET_TARGET_PARAMETERS:
            status = ql_handle_cmd_get_target_parameters(dev);
            break;
        case QL_CMD_GET_FIRMWARE_STATUS:
            status = ql_handle_cmd_get_firmware_status(dev);
            break;
        case QL_CMD_SET_INITIATOR_ID:
            status = ql_handle_cmd_set_initialor_id(dev);
            break;
        case QL_CMD_SET_SELECTION_TIMEOUT:
            status = ql_handle_cmd_set_selection_timeout(dev);
            break;
        case QL_CMD_SET_RETRY_COUNT:
            status = ql_handle_cmd_set_retry_count(dev);
            break;
        case QL_CMD_SET_TAG_AGE_LIMIT:
            status = ql_handle_cmd_set_tag_age_limit(dev);
            break;
        case QL_CMD_SET_CLOCK_RATE:
            status = ql_handle_cmd_set_clock_rate(dev);
            break;
        case QL_CMD_SET_ACTIVE_NEGATION:
            status = ql_handle_cmd_set_active_negation(dev);
            break;
        case QL_CMD_SET_ASYNC_DATA_SETUP:
            status = ql_handle_cmd_set_async_data_setup(dev);
            break;
        case QL_CMD_SET_PCI_CONTROL:
            status = ql_handle_cmd_set_pci_control(dev);
            break;
        case QL_CMD_SET_TARGET_PARAMETERS:
            status = ql_handle_cmd_set_target_parameters(dev);
            break;
        case QL_CMD_SET_DEVICE_QUEUE:
            status = ql_handle_cmd_set_device_queue(dev);
            break;
        case QL_CMD_RETURN_BIOS_BLOCK_ADDR:
            status = ql_handle_cmd_return_bios_block_addr(dev);
            break;
        case QL_CMD_WRITE_FOUR_RAM_WORDS:
            status = ql_handle_cmd_write_four_ram_words(dev);
            break;
        case QL_CMD_EXEC_BIOS_IOCB:
            status = ql_handle_exec_bios_iocb(dev);
            break;
        case QL_CMD_SET_SYSTEM_PARAMETER:
            status = ql_handle_cmd_set_system_parameter(dev);
            break;
        case QL_CMD_SET_FIRMWARE_FEATURES:
            status = ql_handle_cmd_set_firmware_features(dev);
            break;
        case QL_CMD_INIT_REQ_QUEUE_A64:
            status = ql_handle_cmd_init_request_queue(dev, true);
            break;
        case QL_CMD_INIT_RSP_QUEUE_A64:
            status = ql_handle_cmd_init_response_queue(dev, true);
            break;
        case QL_CMD_EXECUTE_IOCB_A64:
            status = ql_handle_exec_mbox_iocb(dev, true);
            break;
        case QL_CMD_GET_TRANSFER_MODE:
            status = ql_handle_cmd_get_transfer_mode(dev);
            break;
        case QL_CMD_SET_DATA_OVERRUN_RECOVERY:
            status = ql_handle_cmd_set_data_overrun_recovery(dev);
            break;

        default:
            ql_log("Unhandled or invalid command %04X\n", dev->reg_mbox_in[0]);
            status = QL_MBOX_STATUS_INVALID;
            break;
    }

    return status;
}

/* RQSTYPE_REQUEST */
static void
ql_pkt_get_sgl_req(uint32_t address, uint32_t idx, isp_data_seg_t *data_seg)
{
    uint32_t offset = address + 32 + idx * 8;

    ql_dma_read32(offset + 0, &data_seg->address);
    ql_dma_read32(offset + 4, &data_seg->length);
}

/* RQSTYPE_REQUEST_A64 */
static void
ql_pkt_get_sgl_req64(uint32_t address, uint32_t idx, isp_data_seg_t *data_seg)
{
    uint32_t offset = address + 36 + idx * 12;

    /* The high part of the address is ignored for 86Box */
    ql_dma_read32(offset + 0, &data_seg->address);
    ql_dma_read32(offset + 8, &data_seg->length);
}

/* RQSTYPE_DATASEG */
static void
ql_pkt_get_sgl_cont(uint32_t address, uint32_t idx, isp_data_seg_t *data_seg)
{
    uint32_t offset = address + 8 + idx * 8;

    ql_dma_read32(offset + 0, &data_seg->address);
    ql_dma_read32(offset + 4, &data_seg->length);
}

/* RQSTYPE_A64_CONT */
static void
ql_pkt_get_sgl_cont64(uint32_t address, uint32_t idx, isp_data_seg_t *data_seg)
{
    uint32_t offset = address + 4 + idx * 12;

    /* The high part of the address is ignored for 86Box */
    ql_dma_read32(offset + 0, &data_seg->address);
    ql_dma_read32(offset + 8, &data_seg->length);
}

static void
ql_pkt_get_entry_type(uint32_t address, uint8_t *entry_type)
{
    ql_dma_read8(address + 0, entry_type);
}

static void
ql_pkt_put_header(uint32_t address, isp_hdr_t *hdr)
{
    ql_dma_write8(address + 0, &hdr->entry_type);
    ql_dma_write8(address + 1, &hdr->entry_count);
    ql_dma_write8(address + 2, &hdr->seqno);
    ql_dma_write8(address + 3, &hdr->flags);
}

static void
ql_pkt_put_request_status(uint32_t address, isp_req_status_t *resp)
{
    ql_pkt_put_header(address, &resp->hdr);

    ql_dma_write32(address + 4, &resp->handle);
    ql_dma_write16(address + 8, &resp->scsi_status);
    ql_dma_write16(address + 10, &resp->comp_status);
    ql_dma_write16(address + 12, &resp->state_flags);
    ql_dma_write16(address + 14, &resp->status_flags);
    ql_dma_write16(address + 16, &resp->time);
    ql_dma_write16(address + 18, &resp->sense_length);
    ql_dma_write32(address + 20, &resp->residual_length);
    ql_dma_write(address + 24, &resp->response[0], sizeof(resp->response));
    ql_dma_write(address + 32, &resp->sense_data[0], sizeof(resp->sense_data));

    ql_log("QL: RESP HDR type 0x%X cnt %u seq %u fl 0x%X\n",
           resp->hdr.entry_type,
           resp->hdr.entry_count,
           resp->hdr.seqno,
           resp->hdr.flags);
    ql_log("QL: RESP h 0x%X scsi 0x%X comp 0x%X state 0x%X statfl 0x%X time %u sens %u resid %u\n",
           resp->handle,
           resp->scsi_status,
           resp->comp_status,
           resp->state_flags,
           resp->status_flags,
           resp->time,
           resp->sense_length,
           resp->residual_length);
}

static bool
ql_sxp_fetch_request(ql_sxp_req_t* pkt, uint32_t address)
{
    /* Fetch the header */
    ql_dma_read8(address + 0, &pkt->hdr.entry_type);
    ql_dma_read8(address + 1, &pkt->hdr.entry_count);
    ql_dma_read8(address + 2, &pkt->hdr.seqno);
    ql_dma_read8(address + 3, &pkt->hdr.flags);

    switch (pkt->hdr.entry_type) {
        case RQSTYPE_REQUEST:
        case RQSTYPE_REQUEST_A64: {
            ql_dma_read32(address + 4, &pkt->handle);
            ql_dma_read8(address + 8, &pkt->lun);
            ql_dma_read8(address + 9, &pkt->bus_target);
            ql_dma_read16(address + 10, &pkt->cdb_length);
            ql_dma_read16(address + 12, &pkt->flags);
            ql_dma_read16(address + 14, &pkt->reserved);
            ql_dma_read16(address + 16, &pkt->timeout);
            ql_dma_read16(address + 18, &pkt->seg_count);
            ql_dma_read(address + 20, &pkt->cdb[0], sizeof(pkt->cdb));
            return true;
        }

        case RQSTYPE_MARKER: {
            ql_dma_read32(address + 4, &pkt->handle);
            ql_dma_read8(address + 8, &pkt->lun);
            ql_dma_read8(address + 9, &pkt->bus_target);
            pkt->timeout = 0;
            return true;
        }

        default:
            break;
    }

    ql_log("QL: Unknown/invalid request type %u\n", pkt->hdr.entry_type);
    return false;
}

static void
ql_sxp_begin_response_entry(ql_sxp_req_t *pkt, isp_req_status_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->hdr.entry_type = RQSTYPE_RESPONSE;
    resp->hdr.entry_count = pkt->hdr.entry_count;
    resp->hdr.seqno = pkt->hdr.seqno;
    resp->hdr.flags = pkt->hdr.flags;
    resp->handle = pkt->handle;
    resp->time = pkt->timeout;
}

static double
ql_sxp_handle_state_send_cdb_bios(ql_t *dev, scsi_device_t *sd)
{
    isp_req_status_t *resp = &dev->pkt_resp;
    double media_period = 10.0;
    uint64_t bytes_xfered = 0;

    if (dev->scsi_data_buffer) {
        free(dev->scsi_data_buffer);
        dev->scsi_data_buffer = NULL;
    }

    if ((sd->phase != SCSI_PHASE_STATUS) && (sd->buffer_length > 0)) {
        uint32_t dev_buffer_length = sd->buffer_length;
        uint8_t *dev_buffer = sd->sc->temp_buffer;
        uint32_t host_buffer_size;
        double p;

        p = scsi_device_get_callback(sd);
        if (p <= 0.0)
            bytes_xfered += sd->buffer_length;
        else
            media_period = p;

        host_buffer_size = dev->cpu_mem[QL_IOCB_FW_BASE + 2];
        host_buffer_size |= (uint32_t)dev->cpu_mem[QL_IOCB_FW_BASE + 3] << 16;

        if (dev->cpu_mem[QL_IOCB_FW_BASE + 12] == 3) {
            /* PIO */
            dev->scsi_data_offset = 0;
            dev->scsi_data_size = dev_buffer_length;
            dev->scsi_data_buffer = malloc(dev_buffer_length);
            memcpy(dev->scsi_data_buffer, dev_buffer, dev_buffer_length);
        } else {
            uint32_t address, block_size;

            address = dev->cpu_mem[QL_IOCB_FW_BASE + 0];
            address |= (uint32_t)dev->cpu_mem[QL_IOCB_FW_BASE + 1] << 16;

            block_size = MIN(host_buffer_size, dev_buffer_length);

            /* DMA */
            if (sd->phase == SCSI_PHASE_DATA_IN) {
                ql_log("QL: DMA to 0x%lx %lu bytes\n", address, block_size);
                ql_dma_write(address, dev_buffer, block_size);
            } else if (sd->phase == SCSI_PHASE_DATA_OUT) {
                ql_log("QL: DMA from 0x%lx %lu bytes\n", address, block_size);
                ql_dma_read(address, dev_buffer, block_size);
            }
        }
        scsi_device_command_phase1(sd);

        if (dev_buffer_length < host_buffer_size) {
            /*
             * Data underrun
             *
             * NOTE: It seems that there is no way to return residual_length to the software.
             * It has been observed that the BIOS does a retry in PIO mode
             * when a DMA underrun error occurs.
             */
            resp->comp_status = RQCS_DATA_UNDERRUN;
        } else if (dev_buffer_length > host_buffer_size) {
            /* Data overrun */
            resp->comp_status = RQCS_DATA_OVERRUN;
        } else {
            /* Normal completion */
            resp->comp_status = RQCS_COMPLETE;
        }
        resp->scsi_status = sd->status;
    } else {
        resp->scsi_status = sd->status;
        resp->comp_status = RQCS_COMPLETE;
    }

    return media_period + (1000000.0 / dev->xfer_rate_bps) * (double)bytes_xfered;
}

static double
ql_sxp_handle_state_send_cdb_sgl(ql_t *dev, scsi_device_t *sd)
{
    ql_sxp_req_t *pkt = &dev->pkt;
    isp_req_status_t *resp = &dev->pkt_resp;
    double media_period = 10.0;
    uint64_t bytes_xfered = QENTRY_LEN;

    /* Read/write command */
    if ((sd->phase != SCSI_PHASE_STATUS) && (sd->buffer_length > 0)) {
        uint32_t dev_buffer_length = sd->buffer_length;
        uint8_t *dev_buffer = sd->sc->temp_buffer;
        uint8_t entry_type = pkt->hdr.entry_type;
        uint32_t pkt_address = dev->pkt_address;
        uint16_t pkt_entry_idx = QL_RQST_CONS(dev);
        uint32_t host_buffer_size = 0;
        uint32_t bytes_moved = 0;
        uint32_t max_seg_count;
        double p;

        p = scsi_device_get_callback(sd);
        if (p <= 0.0)
            bytes_xfered += sd->buffer_length;
        else
            media_period = p;

        if (entry_type == RQSTYPE_REQUEST) {
            max_seg_count = 4;
        } else {
            assert(entry_type == RQSTYPE_REQUEST_A64);
            max_seg_count = 2;
        }

        /* Process the S/G list */
        for (uint32_t i = 0, seg_idx = 0; i < pkt->seg_count; i++, seg_idx++) {
            isp_data_seg_t data_seg;
            uint32_t block_size;

            /* Fetch a continuation segment entry */
            if (seg_idx == max_seg_count) {
                seg_idx = 0;

                pkt_entry_idx = (pkt_entry_idx + 1) % dev->rqst_ring_size;
                pkt_address = dev->rqst_ring_base + pkt_entry_idx * QENTRY_LEN;

                ql_pkt_get_entry_type(pkt_address, &entry_type);
                if (entry_type == RQSTYPE_DATASEG) {
                    max_seg_count = 7;
                } else if (entry_type == RQSTYPE_A64_CONT) {
                    max_seg_count = 5;
                } else {
                    /* Bad entry in the request ring */
                    ql_log("QL: Expected continuation segment but got %u\n", entry_type);
                    break;
                }
            }

            /* Read the S/G fragment */
            switch (entry_type) {
                case RQSTYPE_REQUEST:
                    ql_pkt_get_sgl_req(pkt_address, seg_idx, &data_seg);
                    break;
                case RQSTYPE_REQUEST_A64:
                    ql_pkt_get_sgl_req64(pkt_address, seg_idx, &data_seg);
                    break;
                case RQSTYPE_DATASEG:
                    ql_pkt_get_sgl_cont(pkt_address, seg_idx, &data_seg);
                    break;
                case RQSTYPE_A64_CONT:
                    ql_pkt_get_sgl_cont64(pkt_address, seg_idx, &data_seg);
                    break;

                default:
                    /* Should not happen */
                    assert(false);
                    break;
            }
            host_buffer_size += data_seg.length;
            block_size = MIN(data_seg.length, dev_buffer_length - bytes_moved);
            bytes_xfered += sizeof(data_seg);

            if (sd->phase == SCSI_PHASE_DATA_IN) {
                ql_log("QL: DMA to 0x%lx %lu bytes\n", data_seg.address, block_size);
                ql_dma_write(data_seg.address, &dev_buffer[bytes_moved], block_size);
            } else if (sd->phase == SCSI_PHASE_DATA_OUT) {
                ql_log("QL: DMA from 0x%lx %lu bytes\n", data_seg.address, block_size);
                ql_dma_read(data_seg.address, &dev_buffer[bytes_moved], block_size);
            }
            bytes_moved += block_size;
        }
        scsi_device_command_phase1(sd);

        if (dev_buffer_length < host_buffer_size) {
            /* Data underrun */
            resp->residual_length = host_buffer_size - dev_buffer_length;
            resp->comp_status = RQCS_DATA_UNDERRUN;
        } else if (dev_buffer_length > host_buffer_size) {
            /* Data overrun */
            resp->comp_status = RQCS_DATA_OVERRUN;
        } else {
            /* Normal completion */
            resp->comp_status = RQCS_COMPLETE;
        }
        resp->scsi_status = sd->status;
        resp->state_flags |= RQSF_GOT_STATUS | RQSF_XFER_COMPLETE;
    } else {
        resp->scsi_status = sd->status;
        resp->comp_status = RQCS_COMPLETE;
        resp->state_flags |= RQSF_GOT_STATUS;
    }

    /* Auto request sense */
    if ((resp->scsi_status == SCSI_STATUS_CHECK_CONDITION) && !(pkt->flags & REQFLAG_DISARQ)) {
        if (dev->fw_tid_params[dev->curr_path_id][dev->curr_target_id].flags & DPARM_ARQ) {
            scsi_device_request_sense(sd, resp->sense_data, sizeof(resp->sense_data));

            resp->state_flags |= RQSF_GOT_SENSE;
            resp->sense_length = sizeof(resp->sense_data);

            bytes_xfered += resp->sense_length;
        }
    }

    return media_period + (1000000.0 / dev->xfer_rate_bps) * (double)bytes_xfered;
}

static bool
ql_sxp_state_machine(ql_t *dev)
{
    switch (dev->sxp_state) {
        case SXP_STATE_IDLE: {
            ql_sxp_req_t *pkt = &dev->pkt;

            /* Do we have to pick mailbox registers up? */
            if (dev->sxp_flags & SXP_FLAG_PICK_UP_MBOX) {
                dev->sxp_flags &= ~SXP_FLAG_PICK_UP_MBOX;

                dev->mbox_out_mask = 1 << QL_MBOX_STATUS;
                dev->mbox_data[QL_MBOX_STATUS] = ql_process_mailbox(dev);

                /* Check for mailbox commands that do not need to go through the command processor */
                if (dev->mbox_data[QL_MBOX_STATUS] != QL_MBOX_STATUS_PENDING) {
                    dev->timer_period = QL_MBOX_GENERIC_TIME_US;
                    dev->sxp_state = SXP_STATE_SCHEDULE_MBOX_INTERRUPT;
                    break;
                }

                assert(dev->sxp_flags & (SXP_FLAG_BIOS_IOCB | SXP_FLAG_MBOX_IOCB));

                if (dev->sxp_flags & SXP_FLAG_BIOS_IOCB) {
                    uint8_t lun = dev->cpu_mem[QL_IOCB_FW_BASE + 48] & 0x0F;
                    uint8_t target_id = (dev->cpu_mem[QL_IOCB_FW_BASE + 48] & 0xF0) >> 4 ;
                    uint8_t *cdb_bytes = (uint8_t *)&dev->cpu_mem[QL_IOCB_FW_BASE + 5];

                    /* Translate the BIOS IOCB request into a common structure for easier processing */
                    memset(pkt, 0, sizeof(*pkt));
                    pkt->hdr.entry_type = RQSTYPE_REQUEST;
                    pkt->hdr.entry_count = 1;
                    pkt->lun = lun;
                    pkt->bus_target = target_id;
                    pkt->cdb_length = sizeof(pkt->cdb);
                    memcpy(pkt->cdb, cdb_bytes, sizeof(pkt->cdb));

                    dev->sxp_state = SXP_STATE_SELECT_DEVICE;
                    break;
                }
            }

            if (dev->sxp_flags & SXP_FLAG_MBOX_IOCB) {
                dev->pkt_address = ((uint32_t)dev->reg_mbox_in[2] << 16) | dev->reg_mbox_in[3];
            } else {
                /* Skip processing command queues if they are not initialized */
                if (!(dev->sxp_flags & SXP_FLAG_ENGINE_ACTIVE)) {
                    return false;
                }

                /* No available entries in the request queue, try again later */
                if (QL_RQST_CONS(dev) == QL_RQST_PROD(dev)) {
                    return false;
                }

                /* No room in the response queue, try again later */
                if (((QL_RESP_PROD(dev) + 1) % dev->resp_ring_size) == QL_RESP_CONS(dev)) {
                    return false;
                }

                /* Check is this entry has been aborted */
                if (!fifo8_is_empty(&dev->abort_iocbs_fifo)) {
                    const uint8_t* idx_buf;
                    uint16_t rqst_idx;

                    idx_buf = fifo8_peek_bufptr(&dev->abort_iocbs_fifo, sizeof(rqst_idx), NULL);
                    rqst_idx = ((uint16_t)idx_buf[1] << 8) | idx_buf[0];

                    if (rqst_idx == QL_RQST_CONS(dev)) {
                        fifo8_drop(&dev->abort_iocbs_fifo, sizeof(rqst_idx));

                        dev->sxp_flags |= SXP_FLAG_ABORTED_CMD;

                        dev->timer_period = QL_MBOX_GENERIC_TIME_US;
                        dev->sxp_state = SXP_STATE_COMPLETE_COMMAND;
                        break;
                    }
                }

                dev->pkt_address = dev->rqst_ring_base + QL_RQST_CONS(dev) * QENTRY_LEN;
            }

            if (!ql_sxp_fetch_request(pkt, dev->pkt_address)) {
                /* Skip this packet */
                pkt->hdr.entry_count = 1;

                dev->sxp_state = SXP_STATE_BAD_PACKET;
                break;
            }

            dev->sxp_state = SXP_STATE_SELECT_DEVICE;
            break;
        }
        case SXP_STATE_SELECT_DEVICE: {
            ql_sxp_req_t *pkt = &dev->pkt;

            ql_log("QL: RQST HDR type 0x%X cnt %u seq %u fl 0x%X\n",
                   pkt->hdr.entry_type,
                   pkt->hdr.entry_count,
                   pkt->hdr.seqno,
                   pkt->hdr.flags);
            ql_log("QL: RQST h 0x%X lun %u bus_tid 0x%X cdb_len %u fl 0x%X time %u seq_cnt %u\n",
                   pkt->handle,
                   pkt->lun,
                   pkt->bus_target,
                   pkt->cdb_length,
                   pkt->flags,
                   pkt->timeout,
                   pkt->seg_count);

            /* Extract the SCSI address */
            dev->curr_path_id = (pkt->bus_target & 0x80) >> 7;
            dev->curr_target_id = pkt->bus_target & ~0x80;

            if (dev->curr_path_id >= dev->max_bus_count) {
                dev->sxp_state = SXP_STATE_SELECTION_TIMEOUT;
                break;
            }

            /* 86Box supports one SCSI bus per controller for now */
            if (dev->curr_path_id != 0) {
                dev->sxp_state = SXP_STATE_SELECTION_TIMEOUT;
                break;
            }

            if (dev->curr_target_id >= MIN(QL_MAX_TID, SCSI_ID_MAX)) {
                dev->sxp_state = SXP_STATE_SELECTION_TIMEOUT;
                break;
            }

            if (!scsi_device_present(&scsi_devices[dev->scsi_bus][dev->curr_target_id])) {
                dev->sxp_state = SXP_STATE_SELECTION_TIMEOUT;
                break;
            }

            ql_log("QL: Selected target %u:%u\n", dev->curr_path_id, dev->curr_target_id);
            dev->sxp_state = SXP_STATE_SEND_CDB;
            break;
        }
        case SXP_STATE_SEND_CDB: {
            scsi_device_t *sd = &scsi_devices[dev->scsi_bus][dev->curr_target_id];
            ql_sxp_req_t *pkt = &dev->pkt;
            isp_req_status_t *resp = &dev->pkt_resp;

            ql_sxp_begin_response_entry(pkt, resp);
            resp->state_flags = RQSF_GOT_BUS | RQSF_GOT_TARGET | RQSF_SENT_CDB;

            if (pkt->hdr.entry_type == RQSTYPE_MARKER) {
                resp->scsi_status = SCSI_STATUS_OK;
                resp->comp_status = RQCS_COMMAND_OVERRUN;
                resp->status_flags = RQSTF_ABORTED;

                dev->timer_period = QL_MBOX_GENERIC_TIME_US;
                dev->sxp_state = SXP_STATE_COMPLETE_COMMAND;
                break;
            }

            scsi_device_identify(sd, pkt->lun);

            for (uint32_t i = 0; i < sizeof(pkt->cdb); i++) {
                ql_log("QL: SCSI CDB[%2lu]=%02X\n", i, pkt->cdb[i]);
            }

            sd->buffer_length = -1;
            scsi_device_command_phase0(sd, pkt->cdb);

            if (dev->sxp_flags & SXP_FLAG_BIOS_IOCB) {
                dev->timer_period = ql_sxp_handle_state_send_cdb_bios(dev, sd);
            } else {
                dev->timer_period = ql_sxp_handle_state_send_cdb_sgl(dev, sd);
            }
            scsi_device_identify(sd, SCSI_LUN_USE_CDB);

            dev->sxp_state = SXP_STATE_COMPLETE_COMMAND;
            break;
        }
        case SXP_STATE_SELECTION_TIMEOUT: {
            isp_req_status_t *resp = &dev->pkt_resp;

            ql_sxp_begin_response_entry(&dev->pkt, resp);
            resp->scsi_status = SCSI_STATUS_OK;
            resp->comp_status = RQCS_INCOMPLETE;
            resp->state_flags = RQSF_GOT_BUS;
            resp->status_flags = RQSTF_TIMEOUT;

            dev->timer_period = QL_CMD_SELECTION_TIMEOUT_TIME_US;
            dev->sxp_state = SXP_STATE_COMPLETE_COMMAND;
            break;
        }
        case SXP_STATE_BAD_PACKET: {
            isp_req_status_t *resp = &dev->pkt_resp;

            memset(resp, 0, sizeof(*resp));
            resp->hdr.entry_type = 0;
            resp->hdr.flags = RQSFLAG_BADHEADER;
            resp->comp_status = RQCS_INCOMPLETE;

            dev->timer_period = QL_MBOX_GENERIC_TIME_US;
            dev->sxp_state = SXP_STATE_COMPLETE_COMMAND;
            break;
        }
        case SXP_STATE_COMPLETE_COMMAND: {
            isp_req_status_t *resp = &dev->pkt_resp;
            ql_sxp_req_t *pkt = &dev->pkt;

            /* There are three possible ways of sending SCSI commands to ISP */
            if (dev->sxp_flags & SXP_FLAG_BIOS_IOCB) {
                dev->mbox_out_mask = (1 << QL_MBOX_STATUS) | QL_BIT(1) | QL_BIT(2);
                dev->mbox_data[QL_MBOX_STATUS] = QL_MBOX_STATUS_COMPLETE;
                dev->mbox_data[1] = resp->comp_status;
                dev->mbox_data[2] = resp->scsi_status;

                /* Return the internal queue index */
                dev->cpu_mem[QL_IOCB_FW_BASE + 13] = 0;

                dev->sxp_state = SXP_STATE_SCHEDULE_MBOX_INTERRUPT;
            } else if (dev->sxp_flags & SXP_FLAG_MBOX_IOCB) {
                dev->mbox_out_mask = (1 << QL_MBOX_STATUS);
                dev->mbox_data[QL_MBOX_STATUS] = QL_MBOX_STATUS_COMPLETE;

                /* Mailbox IOCB commands always write a response queue entry */
                dev->sxp_flags |= SXP_FLAG_WRITE_RESP_IOCB;
                dev->sxp_state = SXP_STATE_SCHEDULE_MBOX_INTERRUPT;
            } else {
                uint16_t num_consumed = MAX(pkt->hdr.entry_count, 1);
                QL_RQST_CONS(dev) = (QL_RQST_CONS(dev) + num_consumed) % dev->rqst_ring_size;

                if ((dev->sxp_flags & SXP_FLAG_ABORTED_CMD) || (pkt->hdr.entry_type == RQSTYPE_MARKER)) {
                    /*
                     * Marker entries never raise an interrupt, even if the SCSI device does not exist.
                     * Aborted commands also never raise an interrupt.
                     */
                    dev->sxp_state = SXP_STATE_SCHEDULE_TIMER_SILENCE;
                } else if ((dev->sxp_flags & SXP_FLAG_FAST_POSTING) &&
                           (resp->scsi_status == SCSI_STATUS_OK) &&
                           (resp->comp_status == RQCS_COMPLETE)) {
                    /* Fast posting avoids having to deal with response queue for successful commands */
                    dev->mbox_out_mask = (1 << QL_MBOX_STATUS) | (1 << QL_MBOX_HNDL_LOW) | 1 << (QL_MBOX_HNDL_HIGH);
                    dev->mbox_data[QL_MBOX_STATUS] = QL_ASYNC_STATUS_SCSI_CMD_COMPLETE;
                    dev->mbox_data[QL_MBOX_HNDL_LOW] = pkt->handle & 0xFFFF;
                    dev->mbox_data[QL_MBOX_HNDL_HIGH] = (pkt->handle >> 16) & 0xFFFF;

                    dev->sxp_state = SXP_STATE_SCHEDULE_MBOX_INTERRUPT;
                } else {
                    /* Normal command completion, write a response queue entry */
                    dev->pkt_address = dev->resp_ring_base + QL_RESP_PROD(dev) * QENTRY_LEN;
                    dev->sxp_flags |= SXP_FLAG_WRITE_RESP_IOCB | SXP_FLAG_INC_RESP_RING;
                    dev->sxp_state = SXP_STATE_SCHEDULE_RISC_INTERRUPT;
                }
            }
            break;
        }
        case SXP_STATE_SCHEDULE_RISC_INTERRUPT: {
            dev->sxp_state = SXP_STATE_SET_RISC_INTERRUPT;
            timer_on_auto(&dev->cmd_timer, dev->timer_period);
            return false;
        }
        case SXP_STATE_SCHEDULE_MBOX_INTERRUPT: {
            dev->sxp_state = SXP_STATE_MBOX_WAIT_TIMER;
            timer_on_auto(&dev->cmd_timer, dev->timer_period);
            return false;
        }
        case SXP_STATE_MBOX_WAIT_TIMER: {
            dev->sxp_state = SXP_STATE_ACQUIRE_MBOX_SEMAPHORE;
            break;
        }
        case SXP_STATE_ACQUIRE_MBOX_SEMAPHORE: {
            /* Wait for the mailbox semaphore to be released */
            if (dev->reg_semaphore & QL_SEMAPHORE_LOCK) {
                return false;
            }

            /* Return mailbox registers to the host */
            for (uint32_t i = 0; i < ARRAY_SIZE(dev->reg_mbox_out); i++) {
                if (dev->mbox_out_mask & (1 << i)) {
                    dev->reg_mbox_out[i] = dev->mbox_data[i];
                }
            }
            dev->reg_semaphore = QL_SEMAPHORE_LOCK;
            fallthrough;
        }
        case SXP_STATE_SET_RISC_INTERRUPT: {
            if (dev->sxp_flags & SXP_FLAG_WRITE_RESP_IOCB) {
                ql_pkt_put_request_status(dev->pkt_address, &dev->pkt_resp);

                if (dev->sxp_flags & SXP_FLAG_INC_RESP_RING) {
                    QL_RESP_PROD(dev) = (QL_RESP_PROD(dev) + 1) % dev->resp_ring_size;
                }
            }
            dev->sxp_flags &= ~(SXP_FLAG_WRITE_RESP_IOCB | SXP_FLAG_INC_RESP_RING
                                | SXP_FLAG_BIOS_IOCB | SXP_FLAG_MBOX_IOCB | SXP_FLAG_ABORTED_CMD);

            dev->reg_intr_status |= QL_INTR_REQ | QL_RISC_INTR_REQ;
            ql_update_irq(dev);

            dev->sxp_state = SXP_STATE_IDLE;
            break;
        }
        case SXP_STATE_SCHEDULE_TIMER_SILENCE: {
            dev->sxp_state = SXP_STATE_WAIT_TIMER;
            timer_on_auto(&dev->cmd_timer, dev->timer_period);
            return false;
        }
        case SXP_STATE_WAIT_TIMER: {
            dev->sxp_state = SXP_STATE_IDLE;
            break;
        }

        default: {
            /* Should not happen */
            assert(false);
            break;
        }
    }

    return true;
}

static void
ql_sxp_run_state_machine(ql_t *dev)
{
    ql_log("QL: Enter with %s\n", debug_sxp_state_to_name(dev->sxp_state));

    while (true) {
        SxpState old_state = dev->sxp_state;

        if (!ql_sxp_state_machine(dev)) {
            break;
        }

        if (dev->sxp_state != old_state) {
            ql_log("QL: State %s --> %s\n", debug_sxp_state_to_name(old_state), debug_sxp_state_to_name(dev->sxp_state));
        }
    }

    ql_log("QL: Exit with %s\n", debug_sxp_state_to_name(dev->sxp_state));
}

static void
ql_sxp_kick_engine(ql_t *dev)
{
    if (dev->sxp_state == SXP_STATE_IDLE) {
        ql_log("QL: SCSI kick\n");
        ql_sxp_run_state_machine(dev);
    }
}

static void
ql_sxp_timer_callback(void *priv)
{
    ql_t *dev = priv;

    ql_log("QL: Timer called\n");

    assert(dev->sxp_state != SXP_STATE_IDLE);

    ql_sxp_run_state_machine(dev);
}

static void
ql_sxp_write_semaphore(ql_t *dev, uint16_t val)
{
    if (val == 0) {
        val = QL_SEMAPHORE_STATUS;
    } else {
        val &= QL_SEMAPHORE_STATUS | QL_SEMAPHORE_LOCK;
    }
    dev->reg_semaphore = val;

    if (dev->sxp_state == SXP_STATE_ACQUIRE_MBOX_SEMAPHORE) {
        ql_sxp_run_state_machine(dev);
    }
}

static void
ql_sxp_write_mailbox(ql_t *dev, uint32_t idx, uint16_t val)
{
    dev->reg_mbox_in[idx] = val;

    /* Kick the command processor */
    if ((idx == QL_MBOX_RQST) || (idx == QL_MBOX_RESP)) {
        ql_sxp_kick_engine(dev);
    }
}

static void
ql_write_host_command(ql_t *dev, uint16_t val)
{
    switch (val) {
        case QL_HC_RESET_RISC:
            dev->reg_host_cmd_flags = QL_HC_FLAG_RISC_RESET;
            break;
        case QL_HC_PAUSE_RISC:
            dev->reg_host_cmd_flags |= QL_HC_FLAG_RISC_PAUSE;
            break;
        case QL_HC_RELEASE_RISC:
            dev->reg_host_cmd_flags &= ~(QL_HC_FLAG_RISC_PAUSE | QL_HC_FLAG_RISC_RESET);
            break;
        case QL_HC_SET_HOST_INTR:
            dev->reg_host_cmd_flags |= QL_HC_FLAG_HOST_INTR;
            dev->sxp_flags |= SXP_FLAG_PICK_UP_MBOX;
            ql_sxp_kick_engine(dev);
            break;
        case QL_HC_CLEAR_HOST_INTR:
            /* This command does nothing */
            break;
        case QL_HC_CLEAR_RISC_INTR:
            dev->reg_host_cmd_flags &= ~QL_HC_FLAG_HOST_INTR;
            dev->reg_intr_status = 0;
            ql_update_irq(dev);
            break;
        case QL_HC_DISABLE_BIOS:
            dev->reg_host_cmd_flags = 0;
            break;

        default: {
            ql_log("QL: Unknown host command %04X\n", val);
            break;
        }
    }
}

static void
ql_write_interface_control(ql_t *dev, uint16_t val)
{
    if (val & QL_IFACE_SOFT_RESET) {
        ql_reset_asic(dev);
        return;
    }

    dev->reg_intr_ctrl = val & 0x01FF;
    ql_update_irq(dev);
}

static uint32_t
ql_get_flash_bios_addr(ql_t *dev)
{
    uint32_t address = dev->reg_flash_bios_addr;

    /* Select between two consecutive flash memory banks of 64KB each */
    if (dev->reg_nvram & QL_EEPROM_CS) {
        address |= 0x10000;
    }
    return address;
}

static uint16_t
ql_read_flash_bios_data(ql_t *dev)
{
    uint8_t byte;

    if (!(dev->reg_intr_ctrl & QL_IFACE_FLASH_ENABLE)) {
        return 0xFFFF;
    }

    byte = am29_mmio_read8(ql_get_flash_bios_addr(dev), &dev->flash_device);
    return (byte << 8) | byte;
}

static void
ql_write_flash_bios_data(ql_t *dev, uint16_t value)
{
    if (!(dev->reg_intr_ctrl & QL_IFACE_FLASH_ENABLE)) {
        return;
    }

    am29_mmio_write8(ql_get_flash_bios_addr(dev), (uint8_t)value, &dev->flash_device);
}

static void
ql_write_bank_sxp(ql_t *dev, uint32_t addr, uint32_t bus_number, uint16_t val)
{
    if (bus_number >= dev->max_bus_count) {
        return;
    }

    ql_log("QL: [W16] Unhandled SPX bank write %lX <-- %X\n", IDX_TO_REG(addr), val);
}

static void
ql_write_bank_dma(ql_t *dev, uint32_t addr, uint16_t val)
{
    switch (addr) {
        case QL_REG_CDMA_CFG:
            dev->reg_cdma_cfg = val;
            break;
        case QL_REG_CDMA_CTRL:
            dev->reg_cdma_ctrl = val;
            break;
        case QL_REG_DDMA_CFG:
            dev->reg_ddma_cfg = val;
            break;
        case QL_REG_DDMA_CTRL:
            dev->reg_ddma_ctrl = val;
            break;

        default:
            ql_log("QL: [W16] Unhandled DMA bank write %lX <-- %X\n", IDX_TO_REG(addr), val);
            break;
    }
}

static void
ql_write_bank_risc(ql_t *dev, uint32_t addr, uint16_t val)
{
    switch (addr) {
        case QL_REG_RISC_PSR:
            dev->reg_risc_psr = val;
            break;
        case QL_REG_RISC_PCR:
            dev->reg_risc_pcr = val;
            break;
        case QL_REG_RISC_MTR:
            dev->reg_risc_mtr = val;
            break;
        case QL_REG_HOST_CMD:
            ql_write_host_command(dev, val);
            break;
        case QL_REG_GPIO_DATA:
            dev->reg_gpio_data = val;
            break;
        case QL_REG_GPIO_ENABLE:
            dev->reg_gpio_enable = val;
            break;

        default:
            ql_log("QL: [W16] Unhandled RISC bank write %lX <-- %X\n", IDX_TO_REG(addr), val);
            break;
    }
}

static void
ql_mmio_write16(uint32_t addr, uint16_t val, void* priv)
{
    ql_t *dev = priv;

    /* The hw does not handle unaligned access */
    if (addr & 0x1) {
        return;
    }

    addr &= QL_IO_DECODE_MASK;
    addr = REG_TO_IDX(addr);

#ifdef ENABLE_QL_LOG
    ql_debug_check_for_nvram_access(addr);
#endif

    ql_log("QL: [W16] [%02lX] <-- %4X\n", IDX_TO_REG(addr), val);

    switch (addr) {
        case QL_REG_CFG1:
            dev->reg_cfg1 = val & 0x03FF;
            break;
        case QL_REG_INT_CTRL:
            ql_write_interface_control(dev, val);
            break;
        case QL_REG_SEMAPHORE:
            ql_sxp_write_semaphore(dev, val);
            break;
        case QL_REG_NVRAM:
            dev->reg_nvram = val & (QL_EEPROM_SK | QL_EEPROM_CS | QL_EEPROM_DI);
            nmc93cxx_eeprom_write(dev->eeprom_device,
                                  !!(val & QL_EEPROM_CS),
                                  !!(val & QL_EEPROM_SK),
                                  !!(val & QL_EEPROM_DI));
            break;
        case QL_REG_FLASH_BIOS_DATA:
            ql_write_flash_bios_data(dev, val);
            break;
        case QL_REG_FLASH_BIOS_ADDRESS:
            dev->reg_flash_bios_addr = val;
            break;
        case QL_REG_MAILBOX0:
        case QL_REG_MAILBOX1:
        case QL_REG_MAILBOX2:
        case QL_REG_MAILBOX3:
        case QL_REG_MAILBOX4:
        case QL_REG_MAILBOX5:
        case QL_REG_MAILBOX6:
        case QL_REG_MAILBOX7:
            ql_sxp_write_mailbox(dev, addr - QL_REG_MAILBOX0, val);
            break;

        default: {
            if (dev->isp_type == QL_ISP1040) {
                if (addr >= REG_TO_IDX(0x20) && addr < REG_TO_IDX(0x80)) {
                    /* DMA bank */
                    addr -= REG_TO_IDX(0x20);
                    ql_write_bank_dma(dev, addr, val);
                    break;
                } else if (addr >= REG_TO_IDX(0x80) && addr < REG_TO_IDX(0xFF)) {
                    /* RISC or SXP bank */
                    addr -= REG_TO_IDX(0x80);
                    if (dev->reg_cfg1 & BIU_PCI_CONF1_SXP) {
                        ql_write_bank_sxp(dev, addr, 0, val);
                    } else {
                        ql_write_bank_risc(dev, addr, val);
                    }
                    break;
                }
            } else {
                if (addr >= REG_TO_IDX(0x80) && addr < REG_TO_IDX(0xFF)) {
                    addr -= REG_TO_IDX(0x80);

                    switch (dev->reg_cfg1 & BIU_PCI1080_REG_BANK_MASK) {
                        case BIU_PCI1080_CONF1_SXP0:
                            ql_write_bank_sxp(dev, addr, 0, val);
                            break;
                        case BIU_PCI1080_CONF1_SXP1:
                            ql_write_bank_sxp(dev, addr, 1, val);
                            break;
                        case BIU_PCI1080_CONF1_DMA:
                            ql_write_bank_dma(dev, addr, val);
                            break;
                        default:
                            ql_write_bank_risc(dev, addr, val);
                            break;
                    }
                    break;
                }
            }

            ql_log("QL: [W16] Unhandled write %lX <-- %X\n", IDX_TO_REG(addr), val);
            break;
        }
    }
}

static uint16_t
ql_read_bank_sxp(ql_t *dev, uint32_t addr, uint32_t bus_number)
{
    uint16_t ret;

    if (bus_number >= dev->max_bus_count) {
        return 0;
    }

    switch (addr) {
        case QL_REG_SXP_PINS_DIFF:
            ret = dev->reg_scsi_diff_pins;
            break;

        default:
            ql_log("QL: [R16] Unhandled SXP bank read %lX\n", IDX_TO_REG(addr));
            ret = 0;
            break;
    }

    return ret;
}

static uint16_t
ql_read_bank_dma(ql_t *dev, uint32_t addr)
{
    uint16_t ret;

    switch (addr) {
        case QL_REG_CDMA_CFG:
            ret = dev->reg_cdma_cfg;
            break;
        case QL_REG_CDMA_CTRL:
            ret = dev->reg_cdma_ctrl;
            break;
        case QL_REG_DDMA_CFG:
            ret = dev->reg_ddma_cfg;
            break;
        case QL_REG_DDMA_CTRL:
            ret = dev->reg_ddma_ctrl;
            break;

        case QL_REG_DDMA_DATA_PORT: {
            if (!dev->scsi_data_buffer) {
                ret = 0;
                break;
            }

            /* Return a word via PIO interface */
            ret = 0;
            for (uint32_t i = 0; i < sizeof(uint16_t); i++) {
                uint8_t byte = dev->scsi_data_buffer[dev->scsi_data_offset++];

                ql_log("QL: Return byte 0x%02X at offset %lu\n", byte, dev->scsi_data_offset - 1);

                ret |= (uint16_t)byte << (i * 8);

                /* Last byte, destroy the buffer */
                if (dev->scsi_data_offset >= dev->scsi_data_size) {
                    ql_log("QL: All data has been transmitted\n");

                    free(dev->scsi_data_buffer);
                    dev->scsi_data_buffer = NULL;
                    break;
                }
            }
            break;
        }

        default:
            ql_log("QL: [R16] Unhandled DMA bank read %lX\n", IDX_TO_REG(addr));
            ret = 0;
            break;
    }

    return ret;
}

static uint16_t
ql_read_bank_risc(ql_t *dev, uint32_t addr)
{
    uint16_t ret;

    switch (addr) {
        case QL_REG_RISC_PSR:
            ret = dev->reg_risc_psr;
            break;
        case QL_REG_RISC_PCR:
            ret = dev->reg_risc_pcr;
            break;
        case QL_REG_RISC_PC:
            ret = dev->reg_risc_pc;
            break;
        case QL_REG_RISC_MTR:
            ret = dev->reg_risc_mtr;
            break;
        case QL_REG_HOST_CMD:
            ret = dev->reg_host_cmd_flags;
            break;
        case QL_REG_GPIO_DATA:
            ret = dev->reg_gpio_data;
            break;
        case QL_REG_GPIO_ENABLE:
            ret = dev->reg_gpio_enable;
            break;

        default:
            ql_log("QL: [R16] Unhandled RISC bank read %lX\n", IDX_TO_REG(addr));
            ret = 0;
            break;
    }

    return ret;
}

static uint16_t
ql_mmio_read16(uint32_t addr, void* priv)
{
    ql_t *dev = priv;
    uint16_t ret;

    /* The hw does not handle unaligned access */
    if (addr & 0x1) {
        return 0xFFFF;
    }

    addr &= QL_IO_DECODE_MASK;
    addr = REG_TO_IDX(addr);

    switch (addr) {
        case QL_REG_ID_LOW:
            ret = dev->reg_id_low;
            break;
        case QL_REG_ID_HIGH:
            ret = dev->reg_id_high;
            break;
        case QL_REG_CFG0:
            ret = dev->reg_cfg0;
            break;
        case QL_REG_CFG1:
            ret = dev->reg_cfg1;
            break;
        case QL_REG_INT_CTRL:
            ret = dev->reg_intr_ctrl;
            break;
        case QL_REG_INT_STATUS:
            ret = dev->reg_intr_status;
            break;
        case QL_REG_SEMAPHORE:
            ret = dev->reg_semaphore;
            break;
        case QL_REG_NVRAM:
            ret = dev->reg_nvram;
            if (nmc93cxx_eeprom_read(dev->eeprom_device)) {
                ret |= QL_EEPROM_DO;
            }
            break;
        case QL_REG_FLASH_BIOS_DATA:
            ret = ql_read_flash_bios_data(dev);
            break;
        case QL_REG_FLASH_BIOS_ADDRESS:
            ret = dev->reg_flash_bios_addr;
            break;
        case QL_REG_MAILBOX0:
        case QL_REG_MAILBOX1:
        case QL_REG_MAILBOX2:
        case QL_REG_MAILBOX3:
        case QL_REG_MAILBOX4:
        case QL_REG_MAILBOX5:
        case QL_REG_MAILBOX6:
        case QL_REG_MAILBOX7:
            ret = dev->reg_mbox_out[addr - QL_REG_MAILBOX0];
            break;

        default: {
            uint32_t bank_addr;

            if (dev->isp_type == QL_ISP1040) {
                if (addr >= REG_TO_IDX(0x20) && addr < REG_TO_IDX(0x80)) {
                    /* DMA bank */
                    bank_addr = addr - REG_TO_IDX(0x20);
                    ret = ql_read_bank_dma(dev, bank_addr);
                    break;
                } else if (addr >= REG_TO_IDX(0x80) && addr < REG_TO_IDX(0xFF)) {
                    /* RISC or SXP bank */
                    bank_addr = addr - REG_TO_IDX(0x80);
                    if (dev->reg_cfg1 & BIU_PCI_CONF1_SXP) {
                        ret = ql_read_bank_sxp(dev, bank_addr, 0);
                    } else {
                        ret = ql_read_bank_risc(dev, bank_addr);
                    }
                    break;
                }
            } else {
                if (addr >= REG_TO_IDX(0x80) && addr < REG_TO_IDX(0xFF)) {
                    bank_addr = addr - REG_TO_IDX(0x80);

                    switch (dev->reg_cfg1 & BIU_PCI1080_REG_BANK_MASK) {
                        case BIU_PCI1080_CONF1_SXP0:
                            ret = ql_read_bank_sxp(dev, bank_addr, 0);
                            break;
                        case BIU_PCI1080_CONF1_SXP1:
                            ret = ql_read_bank_sxp(dev, bank_addr, 1);
                            break;
                        case BIU_PCI1080_CONF1_DMA:
                            ret = ql_read_bank_dma(dev, bank_addr);
                            break;
                        default:
                            ret = ql_read_bank_risc(dev, bank_addr);
                            break;
                    }
                    break;
                }
            }

            ql_log("QL: [R16] Unhandled read %lX\n", IDX_TO_REG(addr));
            ret = 0;
            break;
        }
    }

#ifdef ENABLE_QL_LOG
    ql_debug_check_for_nvram_access(addr);
#endif

    ql_log("QL: [R16] [%02lX] --> %4X\n", IDX_TO_REG(addr), ret);
    return ret;
}

static uint8_t
ql_mmio_read8(uint32_t addr, void* priv)
{
    /* The hw only supports 16-bit R/W access */
    return 0xFF;
}

static uint32_t
ql_mmio_read32(uint32_t addr, void* priv)
{
    /* The hw only supports 16-bit R/W access */
    return 0xFFFFFFFF;
}

static void
ql_mmio_write8(uint32_t addr, uint8_t val, void* priv)
{
    /* The hw only supports 16-bit R/W access */
}

static void
ql_mmio_write32(uint32_t addr, uint32_t val, void* priv)
{
    /* The hw only supports 16-bit R/W access */
}

static void
ql_ioport_write32(uint16_t addr, uint32_t val, void *priv)
{
    ql_mmio_write32(addr, val, priv);
}

static void
ql_ioport_write16(uint16_t addr, uint16_t val, void *priv)
{
    ql_mmio_write16(addr, val, priv);
}

static void
ql_ioport_write8(uint16_t addr, uint8_t val, void *priv)
{
    ql_mmio_write8(addr, val, priv);
}

static uint16_t
ql_ioport_read16(uint16_t addr, void* priv)
{
    return ql_mmio_read16(addr, priv);
}

static uint32_t
ql_ioport_read32(uint16_t addr, void* priv)
{
    return ql_mmio_read32(addr, priv);
}

static uint8_t
ql_ioport_read8(uint16_t addr, void* priv)
{
    return ql_mmio_read8(addr, priv);
}

static void
ql_pci_remap_rom_mapping(ql_t *dev, bool do_enable)
{
    uint32_t rom_addr;

    if (do_enable) {
        rom_addr = dev->pci_cfg[PCI_REG_ROM_BAR_BYTE0];
        rom_addr |= dev->pci_cfg[PCI_REG_ROM_BAR_BYTE1] << 8;
        rom_addr |= dev->pci_cfg[PCI_REG_ROM_BAR_BYTE2] << 16;
        rom_addr |= dev->pci_cfg[PCI_REG_ROM_BAR_BYTE3] << 24;
        rom_addr &= ~0x01;

        ql_log("QL: ROM Base %08lX\n", rom_addr);
        mem_mapping_set_addr(&dev->rom_bar_mapping, rom_addr, dev->pci_rom_area_size);
    } else {
        mem_mapping_disable(&dev->rom_bar_mapping);
    }
}

static void
ql_pci_remap_mmio_mapping(ql_t *dev, bool do_enable)
{
    uint32_t mmio_base;

    if (do_enable) {
        mmio_base = dev->pci_cfg[PCI_REG_BAR1_BYTE0];
        mmio_base |= dev->pci_cfg[PCI_REG_BAR1_BYTE1] << 8;
        mmio_base |= dev->pci_cfg[PCI_REG_BAR1_BYTE2] << 16;
        mmio_base |= dev->pci_cfg[PCI_REG_BAR1_BYTE3] << 24;
        mmio_base &= ~0x0F;

        ql_log("QL: MMIO I/O Base %08lX\n", mmio_base);
        mem_mapping_set_addr(&dev->mmio_bar_mapping, mmio_base, QL_PCI_MMIO_BAR_SIZE);
    } else {
        mem_mapping_disable(&dev->mmio_bar_mapping);
    }
}

static void
ql_pci_remap_ioport_mapping(ql_t *dev, bool do_enable)
{
    uint32_t ioport_base;

    ioport_base = dev->pci_cfg[PCI_REG_BAR0_BYTE0];
    ioport_base |= dev->pci_cfg[PCI_REG_BAR0_BYTE1] << 8;
    ioport_base |= dev->pci_cfg[PCI_REG_BAR0_BYTE2] << 16;
    ioport_base |= dev->pci_cfg[PCI_REG_BAR0_BYTE3] << 24;
    ioport_base &= ~0x03;

    if (do_enable) {
        ql_log("QL: I/O Base %08lX\n", ioport_base);
        io_sethandler(ioport_base,
                      QL_PCI_IO_BAR_SIZE,
                      ql_ioport_read8,
                      ql_ioport_read16,
                      ql_ioport_read32,
                      ql_ioport_write8,
                      ql_ioport_write16,
                      ql_ioport_write32,
                      dev);
    } else {
        io_removehandler(ioport_base,
                         QL_PCI_IO_BAR_SIZE,
                         ql_ioport_read8,
                         ql_ioport_read16,
                         ql_ioport_read32,
                         ql_ioport_write8,
                         ql_ioport_write16,
                         ql_ioport_write32,
                         dev);
    }
}

static void
ql_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    ql_t *dev = priv;
    uint8_t write_bits_mask;

    ql_log("QL: PCI [%2X] <-- %X\n", addr, val);

    assert(addr < 256);

    switch (addr) {
        case PCI_REG_COMMAND_L:
            write_bits_mask = 0x57;
            break;
        case PCI_REG_COMMAND_H:
            write_bits_mask = 0x01;
            break;

        case PCI_REG_CACHELINE_SIZE:
            write_bits_mask = 0xFF;
            break;

        case PCI_REG_LATENCY_TIMER:
            write_bits_mask = 0xF8;
            break;

        /* BAR[0] length 0x100 */
        case PCI_REG_BAR0_BYTE0:
            write_bits_mask = 0;
            break;
        case PCI_REG_BAR0_BYTE1:
        case PCI_REG_BAR0_BYTE2:
        case PCI_REG_BAR0_BYTE3:
            write_bits_mask = 0xFF;
            break;

        /* BAR[1] length 0x1000 */
        case PCI_REG_BAR1_BYTE0:
            write_bits_mask = 0;
            break;
        case PCI_REG_BAR1_BYTE1:
            write_bits_mask = 0xF0;
            break;
        case PCI_REG_BAR1_BYTE2:
            write_bits_mask = 0xFF;
            break;
        case PCI_REG_BAR1_BYTE3:
            write_bits_mask = 0xFF;
            break;

        /* ROM BAR */
        case PCI_REG_ROM_BAR_BYTE0:
            write_bits_mask = (uint8_t)(dev->rom_bar_mask >> 0);
            break;
        case PCI_REG_ROM_BAR_BYTE1:
            write_bits_mask = (uint8_t)(dev->rom_bar_mask >> 8);
            break;
        case PCI_REG_ROM_BAR_BYTE2:
            write_bits_mask = (uint8_t)(dev->rom_bar_mask >> 16);
            break;
        case PCI_REG_ROM_BAR_BYTE3:
            write_bits_mask = (uint8_t)(dev->rom_bar_mask >> 24);
            break;

        case PCI_REG_INT_LINE:
            write_bits_mask = 0xFF;
            break;

        case 0x40:
            write_bits_mask = 0xFF;
            break;
        case 0x41:
            write_bits_mask = 0x03;
            break;

        /* PMCSR */
        case QL_PCI_PM_BASE + 4:
            write_bits_mask = dev->has_pci_caps ? 0x03 : 0;
            break;
        case QL_PCI_PM_BASE + 5:
            write_bits_mask = dev->has_pci_caps ? 0x1E : 0;
            break;

        default:
            write_bits_mask = 0;
            break;
    }

    val &= write_bits_mask;

    /* Disable old BAR mapping and handle command change */
    switch (addr) {
        case PCI_REG_COMMAND_L:
            if ((val ^ dev->pci_cfg[addr]) & PCI_COMMAND_IO) {
                ql_pci_remap_ioport_mapping(dev, !!(val & PCI_COMMAND_IO));
            }
            if ((val ^ dev->pci_cfg[addr]) & PCI_COMMAND_MEM) {
                ql_pci_remap_mmio_mapping(dev, !!(val & PCI_COMMAND_MEM));

                if ((val & PCI_COMMAND_MEM) && (dev->pci_cfg[PCI_REG_ROM_BAR_BYTE0] & 0x01)) {
                    ql_pci_remap_rom_mapping(dev, true);
                } else {
                    ql_pci_remap_rom_mapping(dev, false);
                }
            }
            break;

        case PCI_REG_BAR0_BYTE0:
        case PCI_REG_BAR0_BYTE1:
        case PCI_REG_BAR0_BYTE2:
        case PCI_REG_BAR0_BYTE3:
            ql_pci_remap_ioport_mapping(dev, false);
            break;

        case PCI_REG_BAR1_BYTE0:
        case PCI_REG_BAR1_BYTE1:
        case PCI_REG_BAR1_BYTE2:
        case PCI_REG_BAR1_BYTE3:
            ql_pci_remap_mmio_mapping(dev, false);
            break;

        case PCI_REG_ROM_BAR_BYTE0:
        case PCI_REG_ROM_BAR_BYTE1:
        case PCI_REG_ROM_BAR_BYTE2:
        case PCI_REG_ROM_BAR_BYTE3:
            ql_pci_remap_rom_mapping(dev, false);
            break;

        default:
            break;
    }

    /* Update PCI register value */
    val |= dev->pci_cfg[addr] & ~write_bits_mask;
    dev->pci_cfg[addr] = val;

    /* Enable new BAR mapping */
    switch (addr) {
        case PCI_REG_BAR0_BYTE0:
        case PCI_REG_BAR0_BYTE1:
        case PCI_REG_BAR0_BYTE2:
        case PCI_REG_BAR0_BYTE3:
            if (dev->pci_cfg[PCI_REG_COMMAND_L] & PCI_COMMAND_IO) {
                ql_pci_remap_ioport_mapping(dev, true);
            }
            break;

        case PCI_REG_BAR1_BYTE0:
        case PCI_REG_BAR1_BYTE1:
        case PCI_REG_BAR1_BYTE2:
        case PCI_REG_BAR1_BYTE3:
            if (dev->pci_cfg[PCI_REG_COMMAND_L] & PCI_COMMAND_MEM) {
                ql_pci_remap_mmio_mapping(dev, true);
            }
            break;

        case PCI_REG_ROM_BAR_BYTE0:
        case PCI_REG_ROM_BAR_BYTE1:
        case PCI_REG_ROM_BAR_BYTE2:
        case PCI_REG_ROM_BAR_BYTE3:
            if (dev->pci_cfg[PCI_REG_COMMAND_L] & PCI_COMMAND_MEM) {
                if (dev->pci_cfg[PCI_REG_ROM_BAR_BYTE0] & 0x01) {
                    ql_pci_remap_rom_mapping(dev, true);
                }
            }
            break;

        default:
            break;
    }
}

static uint8_t
ql_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    ql_t *dev = priv;
    uint8_t ret;

    assert(addr < 256);

    ret = dev->pci_cfg[addr];

    ql_log("QL: PCI [%2X] --> %X\n", addr, ret);
    return ret;
}

static void ql_init_scsi(ql_t *dev) {
    switch (dev->isp_type) {
        case QL_ISP1040:
            /* Ultra SCSI, 40 MB/s */
            dev->xfer_rate_bps = 40 * 1000000.0;
            dev->max_bus_count = 1;
            break;
        case QL_ISP1080:
            /* Ultra2 SCSI, 80 MB/s */
            dev->xfer_rate_bps = 80 * 1000000.0;
            dev->max_bus_count = 1;
            break;
        case QL_ISP1240:
            /* Ultra SCSI, 40 MB/s */
            dev->xfer_rate_bps = 40 * 1000000.0;
            dev->max_bus_count = 2;
            break;
        case QL_ISP1280:
            /* Ultra2 SCSI, 80 MB/s */
            dev->xfer_rate_bps = 80 * 1000000.0;
            dev->max_bus_count = 2;
            break;
        case QL_ISP12160:
            /* Ultra3 SCSI, 160 MB/s */
            dev->xfer_rate_bps = 160 * 1000000.0;
            dev->max_bus_count = 2;
            break;

        default:
            /* Should not happen */
            assert(false);
            break;
    }

    /* 86Box supports one SCSI bus per controller for now */
    dev->scsi_bus = scsi_get_bus();

    scsi_bus_set_speed(dev->scsi_bus, dev->xfer_rate_bps);

    timer_add(&dev->cmd_timer, ql_sxp_timer_callback, dev, 0);
}

static uint8_t
ql_get_eeprom_checksum(const uint8_t* buffer, size_t size)
{
    size_t i;
    uint8_t crc = 0;

    for (i = 0; i < size - 1; i++) {
        crc += buffer[i];
    }

    return -crc;
}

static void
ql_create_eeprom_image_1040(uint8_t* nvr)
{
    /* ID header */
    nvr[0x00] = 'I';
    nvr[0x01] = 'S';
    nvr[0x02] = 'P';
    nvr[0x03] = ' ';
    /* NVRAM version */
    nvr[0x04] = 7;

    /* ISP config */
    nvr[0x05] = 0x7A;

    /* Bus reset delay */
    nvr[0x06] = 5;
    /* Bus retry count */
    nvr[0x07] = 0;
    /* Bus retry delay */
    nvr[0x08] = 0;

    /* Bus config */
    nvr[0x09] = 0xF9;
    /* Tag age limit */
    nvr[0x0A] = 8;
    /* Bus flags */
    nvr[0x0B] = 0x0B;
    /* Bus selection timeout */
    nvr[0x0C] = 250;
    nvr[0x0D] = 0;
    /* Bus max queue depth */
    nvr[0x0E] = 0x00;
    nvr[0x0F] = 0x01;

    /* Board type */
    nvr[0x10] = 0x17;

    /* System Vendor */
    nvr[0x14] = 0x77;
    nvr[0x15] = 0x10;
    /* System ID */
    nvr[0x12] = 0x01;
    nvr[0x13] = 0x00;

    /* ISP paramrter */
    nvr[0x16] = 0x03;
    nvr[0x17] = 0x00;

    /* FW features */
    nvr[0x18] = 0x01;

    /* Target settings */
    for (uint32_t target_id = 0; target_id < QL_MAX_TID; target_id++) {
        const uint32_t tid_offset = 28 + target_id * 6;

        /* Config */
        nvr[tid_offset + 0] = 0xFD;
        /* Execution throttle */
        nvr[tid_offset + 1] = 16;
        /* Sync period */
        nvr[tid_offset + 2] = 12;
        /* Flags */
        nvr[tid_offset + 3] = 0x18;
    }

    /* System ID offset in words */
    nvr[0x7E] = 0x09;
}

static void
ql_create_eeprom_image_1080(ql_t *dev, uint8_t* nvr)
{
    /* ID header */
    nvr[0x00] = 'I';
    nvr[0x01] = 'S';
    nvr[0x02] = 'P';
    nvr[0x03] = ' ';
    /* NVRAM version */
    nvr[0x04] = 1;

    /* ISP config */
    nvr[0x10] = 0x44;
    /* Bus termination */
    nvr[0x11] = 0x0C;
    /* FW features */
    nvr[0x14] = 0x21;

    /* Bus settings */
    for (uint32_t path_id = 0; path_id < dev->max_bus_count; path_id++) {
        const uint32_t bus_offset = path_id * 112;

        /* Bus config 1 */
        nvr[bus_offset + 0x18] = 0x67;
        /* Bus reset delay */
        nvr[bus_offset + 0x19] = 5;
        /* Bus retry count */
        nvr[bus_offset + 0x1A] = 0;
        /* Bus retry delay */
        nvr[bus_offset + 0x1B] = 0;
        /* Bus config 2 */
        nvr[bus_offset + 0x1C] = 0x39;
        /* Bus selection timeout */
        nvr[bus_offset + 0x1E] = 250;
        nvr[bus_offset + 0x1F] = 0;
        /* Bus max queue depth */
        nvr[bus_offset + 0x20] = 0x00;
        nvr[bus_offset + 0x21] = 0x01;

        /* Target settings */
        for (uint32_t target_id = 0; target_id < QL_MAX_TID; target_id++) {
            const uint32_t tid_offset = bus_offset + 40 + target_id * 6;

            /* Config */
            nvr[tid_offset + 0] = 0xFD;
            /* Execution throttle */
            nvr[tid_offset + 1] = 16;
            /* Sync period */
            nvr[tid_offset + 2] = 10;
            /* Flags */
            if (dev->isp_type == QL_ISP12160) {
                nvr[tid_offset + 3] = 0x30;
            } else {
                nvr[tid_offset + 3] = 0x18;
            }
        }
    }

    /* System Vendor */
    nvr[0xFA] = 0x77;
    nvr[0xFB] = 0x10;

    /* System ID */
    switch (dev->isp_type) {
        case QL_ISP1280:
            nvr[0xFC] = 0x06;
            nvr[0xFD] = 0x00;
            break;
        case QL_ISP12160:
            nvr[0xFC] = 0x07;
            nvr[0xFD] = 0x00;
            break;

        default:
            nvr[0xFC] = 0x01;
            nvr[0xFD] = 0x00;
            break;
    }

    /* System Vendor offset in words */
    if (dev->isp_type != QL_ISP1080) {
        nvr[0xFE] = 0xFA;
    }
}

static void
ql_register_eeprom_device(const device_t *info, ql_t *dev)
{
    int inst = device_get_instance();
    nmc93cxx_eeprom_params_t params;
    char filename[1024] = { 0 };
    nmc93cxx_eeprom_type nvram_type;
    size_t nvram_size;

    if (dev->isp_type == QL_ISP1040) {
        nvram_type = NMC_93C46_x16_64;
        nvram_size = 2 * 64;
    } else {
        nvram_type = NMC_93C56_x16_128;
        nvram_size = 2 * 128;
    }

    uint8_t* nvr = calloc(1, nvram_size);

    if (dev->isp_type == QL_ISP1040) {
        ql_create_eeprom_image_1040(nvr);
    } else {
        ql_create_eeprom_image_1080(dev, nvr);
    }

    /* Checksum */
    nvr[nvram_size - 1] = ql_get_eeprom_checksum(nvr, nvram_size);

    snprintf(filename, sizeof(filename), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name, inst);
    params.type = nvram_type;
    params.default_content = nvr;
    params.filename = filename;
    dev->eeprom_device = device_add_inst_params(&nmc93cxx_device, inst, &params);

    free(nvr);
}

static void
am29_create_flash_image(const device_t *info, flash_t *dev)
{
    FILE *fp;
    size_t bytes_written;
    const char *bios_path;

    dev->array_data = calloc(1, AM29_FLASH_SIZE);

    snprintf(dev->filename,
             sizeof(dev->filename),
             "am29f400_option_rom_%s_%d.bin",
             info->internal_name,
             device_get_instance());

    /* Load the flash image, if it is already present in the system */
    fp = nvr_fopen(dev->filename, "rb");
    if (fp) {
        bytes_written = fread(dev->array_data, 1, AM29_FLASH_SIZE, fp);
    } else {
        bios_path = device_get_bios_file(info, device_get_config_bios(QL_CFG_BIOS_REVISION), 0);

        /* Clone the ROM data to create a new image */
        fp = rom_fopen(bios_path, "rb");
        if (fp) {
            bytes_written = fread(dev->array_data, 1, AM29_FLASH_SIZE, fp);
        } else {
            bytes_written = 0;
            ql_log("Unable to load the AM29 Flash ROM file\n");
        }
    }
    if (fp)
        fclose(fp);

    /* Fill the rest with 0xFF (make the memory content erased) */
    if (bytes_written < AM29_FLASH_SIZE) {
        ql_log("Less than %lu bytes read from the AM29 Flash ROM file\n", (uint32_t)bytes_written);

        memset(dev->array_data + bytes_written, 0xFF, AM29_FLASH_SIZE - bytes_written);
    }
}

static void
am29_update_flash_image(flash_t *dev)
{
    FILE *fp = nvr_fopen(dev->filename, "wb");

    /* Replace the original flash image with new version */
    if (fp) {
        fwrite(dev->array_data, AM29_FLASH_SIZE, 1, fp);
        fclose(fp);
    }

    free(dev->array_data);
}

static void
am29_init(const device_t *info, flash_t *dev)
{
    uint32_t flash_type = (info->local & QL_DEV_FLASH_TYPE_MASK) >> QL_DEV_FLASH_TYPE_SHIFT;
    double access_time_us;

    dev->manufacturer_id = AM29_MANUFACTURER_ID;
    dev->block_select_addr_mask = 0x1C000; // A14-A16

    if (flash_type == QL_FLASH_AM29F010) {
        dev->model_id = AM29F010_MODEL_ID;
        dev->cmd_cycle_addr_mask = 0x1FFFF; // A0-A16
        dev->addr_5555_phys = 0x5555;
        dev->addr_AAAA_phys = 0x2AAA;
        dev->program_time_us = 14.0;
        dev->block_erase_time_us = 200000.0; // 0.2 sec
        dev->chip_erase_time_us = 1000000.0; // 1 sec
        access_time_us = 0.045;
    } else {
        dev->model_id = AM29LV010B_MODEL_ID;
        dev->cmd_cycle_addr_mask = 0x7FF; // A0-A10
        dev->addr_5555_phys = 0x555;
        dev->addr_AAAA_phys = 0x2AA;
        dev->program_time_us = 9.0;
        dev->block_erase_time_us = 200000.0; // 0.2 sec
        dev->chip_erase_time_us = 1000000.0; // 1 sec
        access_time_us = 0.070;
    }
    dev->access_cycles = (cpuclock / (1000000.0 / access_time_us));

    dev->block[0].start_addr  = 0x00000;
    dev->block[0].end_addr    = 0x03FFF;

    dev->block[1].start_addr  = 0x04000;
    dev->block[1].end_addr    = 0x07FFF;

    dev->block[2].start_addr  = 0x08000;
    dev->block[2].end_addr    = 0x0BFFF;

    dev->block[3].start_addr  = 0x0C000;
    dev->block[3].end_addr    = 0x0FFFF;

    dev->block[4].start_addr  = 0x10000;
    dev->block[4].end_addr    = 0x13FFF;

    dev->block[5].start_addr  = 0x14000;
    dev->block[5].end_addr    = 0x17FFF;

    dev->block[6].start_addr  = 0x18000;
    dev->block[6].end_addr    = 0x1BFFF;

    dev->block[7].start_addr  = 0x1C000;
    dev->block[7].end_addr    = 0x1FFFF;

    am29_create_flash_image(info, dev);
    am29_set_mode(dev, M_READ_ARRAY);

    timer_add(&dev->erase_accept_timeout_timer, am29_erase_begin_timer_callback, dev, 0);
    timer_add(&dev->cmd_complete_timer, am29_cmd_complete_timer_callback, dev, 0);

    /* Assign block numbers */
    for (uint32_t i = 0; i < AM29_MAX_BLOCKS; i++) {
        flash_block_t *block = &dev->block[i];

        /* We maintain a bitmap of blocks to erase */
        assert(i < 32);
        assert(sizeof(dev->blocks_to_erase_bitmap) == sizeof(uint32_t));

        block->number = i;
    }
}

static void
ql_init_pci_config(ql_t *dev)
{
    const uint8_t *eeprom_data = (const uint8_t *)nmc93cxx_eeprom_data(dev->eeprom_device);

    memset(dev->pci_cfg, 0, sizeof(dev->pci_cfg));

    dev->pci_cfg[PCI_REG_STATUS_L] = PCI_STATUS_L_FAST_B2B | PCI_STATUS_L_CAPAB;
    dev->pci_cfg[PCI_REG_STATUS_H] = PCI_DEVSEL_MEDIUM;

    /* QLA1xxx */
    switch (dev->isp_type) {
        case QL_ISP1040:
            dev->pci_cfg[PCI_REG_DEVICE_ID_L] = 0x20;
            dev->pci_cfg[PCI_REG_DEVICE_ID_H] = 0x10;
            dev->pci_cfg[PCI_REG_REVISION] = 0x05;
            break;
        case QL_ISP1080:
            dev->pci_cfg[PCI_REG_DEVICE_ID_L] = 0x80;
            dev->pci_cfg[PCI_REG_DEVICE_ID_H] = 0x10;
            dev->pci_cfg[PCI_REG_REVISION] = 0x01;
            break;
        case QL_ISP1240:
            dev->pci_cfg[PCI_REG_DEVICE_ID_L] = 0x40;
            dev->pci_cfg[PCI_REG_DEVICE_ID_H] = 0x12;
            dev->pci_cfg[PCI_REG_REVISION] = 0x01;
            break;
        case QL_ISP1280:
            dev->pci_cfg[PCI_REG_DEVICE_ID_L] = 0x80;
            dev->pci_cfg[PCI_REG_DEVICE_ID_H] = 0x12;
            dev->pci_cfg[PCI_REG_REVISION] = 0x01;
            break;
        case QL_ISP12160:
            dev->pci_cfg[PCI_REG_DEVICE_ID_L] = 0x16;
            dev->pci_cfg[PCI_REG_DEVICE_ID_H] = 0x12;
            dev->pci_cfg[PCI_REG_REVISION] = 0x06;
            dev->pci_cfg[PCI_REG_STATUS_L] |= PCI_STATUS_L_66MHZ;
            break;

        default:
            /* Should not happen */
            assert(false);
            break;
    }

    /* Actual system ID comes from NVRAM. The ISP1040 system ID words are in swapped order */
    if (dev->isp_type == QL_ISP1040) {
        dev->pci_cfg[PCI_REG_SUBVEN_ID_L] = eeprom_data[0x14];
        dev->pci_cfg[PCI_REG_SUBVEN_ID_H] = eeprom_data[0x15];
        dev->pci_cfg[PCI_REG_SUBSYS_ID_L] = eeprom_data[0x12];
        dev->pci_cfg[PCI_REG_SUBSYS_ID_H] = eeprom_data[0x13];
    } else {
        dev->pci_cfg[PCI_REG_SUBVEN_ID_L] = eeprom_data[0xFA];
        dev->pci_cfg[PCI_REG_SUBVEN_ID_H] = eeprom_data[0xFB];
        dev->pci_cfg[PCI_REG_SUBSYS_ID_L] = eeprom_data[0xFC];
        dev->pci_cfg[PCI_REG_SUBSYS_ID_H] = eeprom_data[0xFD];
    }

    /* QLogic */
    dev->pci_cfg[PCI_REG_VENDOR_ID_L] = 0x77;
    dev->pci_cfg[PCI_REG_VENDOR_ID_H] = 0x10;

    /* SCSI Controller */
    dev->pci_cfg[PCI_REG_CLASS] = 0x01;

    dev->pci_cfg[PCI_REG_CACHELINE_SIZE] = 64;
    dev->pci_cfg[PCI_REG_LATENCY_TIMER] = 248;
    dev->pci_cfg[PCI_REG_INT_PIN] = PCI_INTA;

    /* BAR[0] I/O ports */
    dev->pci_cfg[PCI_REG_BAR0_BYTE0] = 0x01;

    /* BAR[1] Memory */
    dev->pci_cfg[PCI_REG_BAR1_BYTE0] = 0;

    dev->pci_cfg[0x40] = 0x44;

    if (dev->has_pci_caps) {
        dev->pci_cfg[PCI_REG_STATUS_L] |= PCI_STATUS_L_CAPAB;

        dev->pci_cfg[PCI_REG_CAPS_PTR] = QL_PCI_PM_BASE;

        /* Power management capabilities */
        dev->pci_cfg[QL_PCI_PM_BASE + 0] = 0x01; // POWER MANAGEMENT
        dev->pci_cfg[QL_PCI_PM_BASE + 1] = 0x00; // Last entry
        /* PMC */
        dev->pci_cfg[QL_PCI_PM_BASE + 2] = 0x01; // Version 1.0
        dev->pci_cfg[QL_PCI_PM_BASE + 3] = 0x00;
        /* PMCSR */
        dev->pci_cfg[QL_PCI_PM_BASE + 4] = 0x00;
        dev->pci_cfg[QL_PCI_PM_BASE + 5] = 0x00;
        /* PMCSR_BSE */
        dev->pci_cfg[QL_PCI_PM_BASE + 6] = 0x00;
        /* Data */
        dev->pci_cfg[QL_PCI_PM_BASE + 7] = 0x00;
    }

    /* This area for some reason holds the VenID/DevID pair */
    for (uint32_t reg = 0x4C; reg < sizeof(dev->pci_cfg); reg += 4) {
        dev->pci_cfg[reg + 0] = dev->pci_cfg[PCI_REG_VENDOR_ID_L];
        dev->pci_cfg[reg + 1] = dev->pci_cfg[PCI_REG_VENDOR_ID_H];
        dev->pci_cfg[reg + 2] = dev->pci_cfg[PCI_REG_DEVICE_ID_L];
        dev->pci_cfg[reg + 3] = dev->pci_cfg[PCI_REG_DEVICE_ID_H];
    }
}

static void
ql_reset(void *priv)
{
    ql_t *dev = priv;

    /* Clear all BAR memory mappings and I/O handlers */
    ql_pci_remap_ioport_mapping(dev, false);
    ql_pci_remap_mmio_mapping(dev, false);
    ql_pci_remap_rom_mapping(dev, false);

    /* Reset PCI configuration registers */
    ql_init_pci_config(dev);

    ql_reset_asic(dev);
    am29_reset(&dev->flash_device);
}

static void *
ql_init(const device_t *info)
{
    ql_t *dev = calloc(1, sizeof(ql_t));

    dev->isp_type = info->local & QL_DEV_CHIP_TYPE_MASK;
    dev->isp_rev = (info->local & QL_DEV_CHIP_REV_MASK) >> QL_DEV_CHIP_REV_SHIFT;

    dev->fw_version = device_get_bios_local(device_context_get_device(), device_get_config_bios(QL_CFG_BIOS_REVISION));
    dev->has_pci_caps = (dev->isp_type != QL_ISP1040);

    /*
     * Determine size of the area to map the expansion ROM.
     * NOTE: On most ISP chips this area is smaller than the FLASH size.
     * The QLogic boot code reads the required data directly from the FLASH at run-time.
     */
    if (dev->isp_type == QL_ISP12160) {
        dev->pci_rom_area_size = QL_PCI_ROM_BAR_128K_SIZE;
    } else {
        dev->pci_rom_area_size = QL_PCI_ROM_BAR_64K_SIZE;
    }

    /* Determine writable bits of the ROM BAR */
    if (!device_get_config_int(QL_CFG_BIOS_ENABLE)) {
        dev->rom_bar_mask = 0;
    } else {
        uint32_t length = dev->pci_rom_area_size;
        uint32_t ln2size = 0;

        while (length != 1) {
            ln2size++;
            length >>= 1;
        }

        dev->rom_bar_mask = ~((1 << ln2size) - 1);
        dev->rom_bar_mask |= 1; // Expansion ROM enable bit
    }

    ql_init_scsi(dev);
    am29_init(info, &dev->flash_device);
    ql_register_eeprom_device(info, dev);
    ql_init_pci_config(dev);
    ql_reset_asic(dev);

    mem_mapping_add(&dev->rom_bar_mapping,
                    0,
                    0,
                    ql_rom_bar_mmio_read8,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    dev);
    mem_mapping_disable(&dev->rom_bar_mapping);

    mem_mapping_add(&dev->mmio_bar_mapping,
                    0,
                    0,
                    ql_mmio_read8,
                    ql_mmio_read16,
                    ql_mmio_read32,
                    ql_mmio_write8,
                    ql_mmio_write16,
                    ql_mmio_write32,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    dev);
    mem_mapping_disable(&dev->mmio_bar_mapping);

    pci_add_card(PCI_CARD_NORMAL, ql_pci_read, ql_pci_write, dev, &dev->pci_slot);
    return dev;
}

static void
ql_close(void *priv)
{
    ql_t *dev = priv;

    am29_update_flash_image(&dev->flash_device);

    fifo8_destroy(&dev->abort_iocbs_fifo);

    if (dev->scsi_data_buffer) {
        free(dev->scsi_data_buffer);
    }
    free(dev);
}

// clang-format off
static const device_config_t qla1040b_config[] = {
    {
        .name           = QL_CFG_BIOS_REVISION,
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v6_26",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 6.20",
                .internal_name = "v6_20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(4, 53, 0),
                .size          = 0x10000,
                .files         = { "roms/scsi/qlogic/qla1040_v6_20.bin", "" }
            },
            {
                .name          = "Version 6.26",
                .internal_name = "v6_26",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(4, 55, 0),
                .size          = 0x10000,
                .files         = { "roms/scsi/qlogic/qla1040_v6_26.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = QL_CFG_BIOS_ENABLE,
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t qla1080_config[] = {
    {
        .name           = QL_CFG_BIOS_REVISION,
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v1_19",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 1.11",
                .internal_name = "v1_11",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(2, 13, 0),
                .size          = 0x10000,
                .files         = { "roms/scsi/qlogic/qla1080_v1_11.bin", "" }
            },
            {
                .name          = "Version 1.16",
                .internal_name = "v1_16",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(8, 3, 0),
                .size          = 0x20000,
                .files         = { "roms/scsi/qlogic/qla1080_v1_16.bin", "" }
            },
            {
                .name          = "Version 1.19",
                .internal_name = "v1_19",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(8, 9, 0),
                .size          = 0x20000,
                .files         = { "roms/scsi/qlogic/qla1080_v1_19.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = QL_CFG_BIOS_ENABLE,
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t qla1240_config[] = {
    {
        .name           = QL_CFG_BIOS_REVISION,
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v1_26",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 1.26",
                .internal_name = "v1_26",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(2, 13, 0),
                .size          = 0x10000,
                .files         = { "roms/scsi/qlogic/qla1240_v1_26.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = QL_CFG_BIOS_ENABLE,
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t qla1280_config[] = {
    {
        .name           = QL_CFG_BIOS_REVISION,
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v1_30",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 1.30",
                .internal_name = "v1_30",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(8, 15, 0),
                .size          = 0x20000,
                .files         = { "roms/scsi/qlogic/qla1280_v1_30.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = QL_CFG_BIOS_ENABLE,
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t qla12160a_config[] = {
    {
        .name           = QL_CFG_BIOS_REVISION,
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v1_37",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 1.34",
                .internal_name = "v1_34",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(10, 4, 0),
                .size          = 0x20000,
                .files         = { "roms/scsi/qlogic/qla12160_v1_34.bin", "" }
            },
            {
                .name          = "Version 1.37",
                .internal_name = "v1_37",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = ISP_FW_VER(10, 4, 0),
                .size          = 0x20000,
                .files         = { "roms/scsi/qlogic/qla12160_v1_37.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = QL_CFG_BIOS_ENABLE,
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t qla1040b_device = {
    .name          = "QLogic QLA1040B",
    .internal_name = "qla1040b",
    .flags         = DEVICE_PCI,
    .local         = QL_ISP1040 | QL_REV_ISP1040B | QL_FLASH_AM29F010,
    .init          = ql_init,
    .close         = ql_close,
    .reset         = ql_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = qla1040b_config,
};

const device_t qla1080_device = {
    .name          = "QLogic QLA1080",
    .internal_name = "qla1080",
    .flags         = DEVICE_PCI,
    .local         = QL_ISP1080 | QL_REV_ISP1080 | QL_FLASH_AM29F010,
    .init          = ql_init,
    .close         = ql_close,
    .reset         = ql_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = qla1080_config,
};

const device_t qla1240_device = {
    .name          = "QLogic QLA1240",
    .internal_name = "qla1240",
    .flags         = DEVICE_PCI,
    .local         = QL_ISP1240 | QL_REV_ISP1080 | QL_FLASH_AM29LV010B,
    .init          = ql_init,
    .close         = ql_close,
    .reset         = ql_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = qla1240_config,
};

const device_t qla1280_device = {
    .name          = "QLogic QLA1280",
    .internal_name = "qla1280",
    .flags         = DEVICE_PCI,
    .local         = QL_ISP1280 | QL_REV_ISP1080 | QL_FLASH_AM29LV010B,
    .init          = ql_init,
    .close         = ql_close,
    .reset         = ql_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = qla1280_config,
};

const device_t qla12160a_device = {
    .name          = "QLogic QLA12160A",
    .internal_name = "qla12160a",
    .flags         = DEVICE_PCI,
    .local         = QL_ISP12160 | QL_REV_ISP1080 | QL_FLASH_AM29LV010B,
    .init          = ql_init,
    .close         = ql_close,
    .reset         = ql_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = qla12160a_config,
};
