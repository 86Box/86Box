/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of BusLogic ISA and PCI SCSI controllers. Boards
 *          supported:
 *
 *            0 - BT-542BH ISA;
 *            1 - BT-545S ISA;
 *            2 - BT-958D PCI
 *
 *
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/mca.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_buslogic.h>
#include <86box/scsi_device.h>
#include <86box/scsi_x54x.h>

/*
 * Auto SCSI structure which is located
 * in host adapter RAM and contains several
 * configuration parameters.
 */
#pragma pack(push, 1)
typedef struct AutoSCSIRam_t {
    uint8_t       aInternalSignature[2];
    uint8_t       cbInformation;
    uint8_t       aHostAdaptertype[6];
    uint8_t       uReserved1;
    uint8_t       fFloppyEnabled           : 1;
    uint8_t       fFloppySecondary         : 1;
    uint8_t       fLevelSensitiveInterrupt : 1;
    uint8_t       uReserved2               : 2;
    uint8_t       uSystemRAMAreForBIOS     : 3;
    uint8_t       uDMAChannel              : 7;
    uint8_t       fDMAAutoConfiguration    : 1;
    uint8_t       uIrqChannel              : 7;
    uint8_t       fIrqAutoConfiguration    : 1;
    uint8_t       uDMATransferRate;
    uint8_t       uSCSIId;
    uint8_t       uSCSIConfiguration;
    uint8_t       uBusOnDelay;
    uint8_t       uBusOffDelay;
    uint8_t       uBIOSConfiguration;
    uint16_t      u16DeviceEnabledMask;
    uint16_t      u16WidePermittedMask;
    uint16_t      u16FastPermittedMask;
    uint16_t      u16SynchronousPermittedMask;
    uint16_t      u16DisconnectPermittedMask;
    uint16_t      u16SendStartUnitCommandMask;
    uint16_t      u16IgnoreInBIOSScanMask;
    unsigned char uPCIInterruptPin              : 2;
    unsigned char uHostAdapterIoPortAddress     : 2;
    uint8_t       fRoundRobinScheme             : 1;
    uint8_t       fVesaBusSpeedGreaterThan33MHz : 1;
    uint8_t       fVesaBurstWrite               : 1;
    uint8_t       fVesaBurstRead                : 1;
    uint16_t      u16UltraPermittedMask;
    uint32_t      uReserved5;
    uint8_t       uReserved6;
    uint8_t       uAutoSCSIMaximumLUN;
    uint8_t       fReserved7                   : 1;
    uint8_t       fSCAMDominant                : 1;
    uint8_t       fSCAMenabled                 : 1;
    uint8_t       fSCAMLevel2                  : 1;
    unsigned char uReserved8                   : 4;
    uint8_t       fInt13Extension              : 1;
    uint8_t       fReserved9                   : 1;
    uint8_t       fCDROMBoot                   : 1;
    unsigned char uReserved10                  : 2;
    uint8_t       fMultiBoot                   : 1;
    unsigned char uReserved11                  : 2;
    unsigned char uBootTargetId                : 4;
    unsigned char uBootChannel                 : 4;
    uint8_t       fForceBusDeviceScanningOrder : 1;
    unsigned char uReserved12                  : 7;
    uint16_t      u16NonTaggedToAlternateLunPermittedMask;
    uint16_t      u16RenegotiateSyncAfterCheckConditionMask;
    uint8_t       aReserved14[10];
    uint8_t       aManufacturingDiagnostic[2];
    uint16_t      u16Checksum;
} AutoSCSIRam;
#pragma pack(pop)

/* The local RAM. */
#pragma pack(push, 1)
typedef union {
    uint8_t u8View[256];          /* byte view */
    struct {                      /* structured view */
        uint8_t     u8Bios[64];   /* offset 0 - 63 is for BIOS */
        AutoSCSIRam autoSCSIData; /* Auto SCSI structure */
    } structured;
} HALocalRAM;
#pragma pack(pop)

/** Structure for the INQUIRE_SETUP_INFORMATION reply. */
#pragma pack(push, 1)
typedef struct {
    uint8_t                                      uSignature;
    uint8_t                                      uCharacterD;
    uint8_t                                      uHostBusType;
    uint8_t                                      uWideTransferPermittedId0To7;
    uint8_t                                      uWideTransfersActiveId0To7;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId8To15[8];
    uint8_t                                      uDisconnectPermittedId8To15;
    uint8_t                                      uReserved2;
    uint8_t                                      uWideTransferPermittedId8To15;
    uint8_t                                      uWideTransfersActiveId8To15;
} buslogic_setup_t;
#pragma pack(pop)

/* Structure for the INQUIRE_EXTENDED_SETUP_INFORMATION. */
#pragma pack(push, 1)
typedef struct ReplyInquireExtendedSetupInformation_t {
    uint8_t  uBusType;
    uint8_t  uBiosAddress;
    uint16_t u16ScatterGatherLimit;
    uint8_t  cMailbox;
    uint32_t uMailboxAddressBase;
    uint8_t  uReserved1               : 2;
    uint8_t  fFastEISA                : 1;
    uint8_t  uReserved2               : 3;
    uint8_t  fLevelSensitiveInterrupt : 1;
    uint8_t  uReserved3               : 1;
    uint8_t aFirmwareRevision[3];
    uint8_t fHostWideSCSI          : 1;
    uint8_t  fHostDifferentialSCSI : 1;
    uint8_t  fHostSupportsSCAM     : 1;
    uint8_t  fHostUltraSCSI        : 1;
    uint8_t  fHostSmartTermination : 1;
    uint8_t  uReserved4            : 3;
} ReplyInquireExtendedSetupInformation;
#pragma pack(pop)

/* Structure for the INQUIRE_PCI_HOST_ADAPTER_INFORMATION reply. */
#pragma pack(push, 1)
typedef struct BuslogicPCIInformation_t {
    uint8_t IsaIOPort;
    uint8_t IRQ;
    uint8_t LowByteTerminated  : 1;
    uint8_t HighByteTerminated : 1;
    uint8_t uReserved          : 2; /* Reserved. */
    uint8_t JP1                : 1; /* Whatever that means. */
    uint8_t JP2                : 1; /* Whatever that means. */
    uint8_t JP3                : 1; /* Whatever that means. */
    uint8_t InformationIsValid : 1;
    uint8_t uReserved2;             /* Reserved. */
} BuslogicPCIInformation_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct ESCMD_t {
    /** Data length. */
    uint32_t DataLength;
    /** Data pointer. */
    uint32_t DataPointer;
    /** The device the request is sent to. */
    uint8_t TargetId;
    /** The LUN in the device. */
    uint8_t LogicalUnit;
    /** Reserved */
    unsigned char Reserved1 : 3;
    /** Data direction for the request. */
    unsigned char DataDirection : 2;
    /** Reserved */
    unsigned char Reserved2 : 3;
    /** Length of the SCSI CDB. */
    uint8_t CDBLength;
    /** The SCSI CDB.  (A CDB can be 12 bytes long.)   */
    uint8_t CDB[12];
} ESCMD;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct MailboxInitExtended_t {
    uint8_t  Count;
    uint32_t Address;
} MailboxInitExtended_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct buslogic_data_t {
    rom_t      bios;
    int        ExtendedLUNCCBFormat;
    int        fAggressiveRoundRobinMode;
    HALocalRAM LocalRAM;
    int        PCIBase;
    int        MMIOBase;
    int        chip;
    int        has_bios;
    uint32_t   bios_addr;
    uint32_t   bios_size;
    uint32_t   bios_mask;
    uint8_t    AutoSCSIROM[32768];
    uint8_t    SCAMData[65536];
} buslogic_data_t;
#pragma pack(pop)

enum {
    CHIP_BUSLOGIC_ISA_542B_1991_12_14,
    CHIP_BUSLOGIC_ISA_545S_1992_10_05,
    CHIP_BUSLOGIC_ISA_542BH_1993_05_23,
    CHIP_BUSLOGIC_ISA_545C_1994_12_01,
    CHIP_BUSLOGIC_VLB_445S_1993_11_16,
    CHIP_BUSLOGIC_VLB_445C_1994_12_01,
    CHIP_BUSLOGIC_MCA_640A_1993_05_23,
    CHIP_BUSLOGIC_PCI_958D_1995_12_30
};

#ifdef ENABLE_BUSLOGIC_LOG
int buslogic_do_log = ENABLE_BUSLOGIC_LOG;

static void
buslogic_log(const char *fmt, ...)
{
    va_list ap;

    if (buslogic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define buslogic_log(fmt, ...)
#endif

static char *
BuslogicGetNVRFileName(buslogic_data_t *bl)
{
    switch (bl->chip) {
        case CHIP_BUSLOGIC_ISA_542B_1991_12_14:
            return "bt542b.nvr";
        case CHIP_BUSLOGIC_ISA_545S_1992_10_05:
            return "bt545s.nvr";
        case CHIP_BUSLOGIC_ISA_542BH_1993_05_23:
            return "bt542bh.nvr";
        case CHIP_BUSLOGIC_ISA_545C_1994_12_01:
            return "bt545c.nvr";
        case CHIP_BUSLOGIC_VLB_445S_1993_11_16:
            return "bt445s.nvr";
        case CHIP_BUSLOGIC_VLB_445C_1994_12_01:
            return "bt445c.nvr";
        case CHIP_BUSLOGIC_MCA_640A_1993_05_23:
            return "bt640a.nvr";
        case CHIP_BUSLOGIC_PCI_958D_1995_12_30:
            return "bt958d.nvr";
        default:
            fatal("Unrecognized BusLogic chip: %i\n", bl->chip);
            return NULL;
    }
}

static void
BuslogicAutoSCSIRamSetDefaults(x54x_t *dev, uint8_t safe)
{
    buslogic_data_t *bl   = (buslogic_data_t *) dev->ven_data;
    HALocalRAM      *HALR = &bl->LocalRAM;

    memset(&(HALR->structured.autoSCSIData), 0, sizeof(AutoSCSIRam));

    HALR->structured.autoSCSIData.aInternalSignature[0] = 'F';
    HALR->structured.autoSCSIData.aInternalSignature[1] = 'A';

    HALR->structured.autoSCSIData.cbInformation = 64;

    HALR->structured.autoSCSIData.uReserved1 = 6;

    HALR->structured.autoSCSIData.aHostAdaptertype[0] = ' ';
    HALR->structured.autoSCSIData.aHostAdaptertype[5] = ' ';
    switch (bl->chip) {
        case CHIP_BUSLOGIC_ISA_542B_1991_12_14:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "542B", 4);
            break;
        case CHIP_BUSLOGIC_ISA_545S_1992_10_05:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "545S", 4);
            break;
        case CHIP_BUSLOGIC_ISA_542BH_1993_05_23:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "542BH", 5);
            break;
        case CHIP_BUSLOGIC_ISA_545C_1994_12_01:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "545C", 4);
            break;
        case CHIP_BUSLOGIC_VLB_445S_1993_11_16:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "445S", 4);
            break;
        case CHIP_BUSLOGIC_VLB_445C_1994_12_01:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "445C", 4);
            break;
        case CHIP_BUSLOGIC_MCA_640A_1993_05_23:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "640A", 4);
            break;
        case CHIP_BUSLOGIC_PCI_958D_1995_12_30:
            memcpy(&(HALR->structured.autoSCSIData.aHostAdaptertype[1]), "958D", 4);
            break;

        default:
            break;
    }

    HALR->structured.autoSCSIData.fLevelSensitiveInterrupt = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 1 : 0;
    HALR->structured.autoSCSIData.uSystemRAMAreForBIOS     = 6;

    if (bl->chip != CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
        switch (dev->DmaChannel) {
            case 5:
                HALR->structured.autoSCSIData.uDMAChannel = 1;
                break;
            case 6:
                HALR->structured.autoSCSIData.uDMAChannel = 2;
                break;
            case 7:
                HALR->structured.autoSCSIData.uDMAChannel = 3;
                break;
            default:
                HALR->structured.autoSCSIData.uDMAChannel = 0;
                break;
        }
    }
    HALR->structured.autoSCSIData.fDMAAutoConfiguration = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 0 : 1;

    if (bl->chip != CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
        switch (dev->Irq) {
            case 9:
                HALR->structured.autoSCSIData.uIrqChannel = 1;
                break;
            case 10:
                HALR->structured.autoSCSIData.uIrqChannel = 2;
                break;
            case 11:
                HALR->structured.autoSCSIData.uIrqChannel = 3;
                break;
            case 12:
                HALR->structured.autoSCSIData.uIrqChannel = 4;
                break;
            case 14:
                HALR->structured.autoSCSIData.uIrqChannel = 5;
                break;
            case 15:
                HALR->structured.autoSCSIData.uIrqChannel = 6;
                break;
            default:
                HALR->structured.autoSCSIData.uIrqChannel = 0;
                break;
        }
    }
    HALR->structured.autoSCSIData.fIrqAutoConfiguration = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 0 : 1;

    HALR->structured.autoSCSIData.uDMATransferRate = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 0 : 1;

    HALR->structured.autoSCSIData.uSCSIId            = 7;
    HALR->structured.autoSCSIData.uSCSIConfiguration = 0x3F;
    HALR->structured.autoSCSIData.uBusOnDelay        = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 0 : 7;
    HALR->structured.autoSCSIData.uBusOffDelay       = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 0 : 4;
    HALR->structured.autoSCSIData.uBIOSConfiguration = (bl->has_bios) ? 0x33 : 0x32;
    if (!safe)
        HALR->structured.autoSCSIData.uBIOSConfiguration |= 0x04;

    HALR->structured.autoSCSIData.u16DeviceEnabledMask       = 0xffff;
    HALR->structured.autoSCSIData.u16WidePermittedMask       = 0xffff;
    HALR->structured.autoSCSIData.u16FastPermittedMask       = 0xffff;
    HALR->structured.autoSCSIData.u16DisconnectPermittedMask = 0xffff;

    HALR->structured.autoSCSIData.uPCIInterruptPin              = PCI_INTA;
    HALR->structured.autoSCSIData.fVesaBusSpeedGreaterThan33MHz = 1;

    HALR->structured.autoSCSIData.uAutoSCSIMaximumLUN = 7;

    HALR->structured.autoSCSIData.fForceBusDeviceScanningOrder = 1;
    HALR->structured.autoSCSIData.fInt13Extension              = safe ? 0 : 1;
    HALR->structured.autoSCSIData.fCDROMBoot                   = safe ? 0 : 1;
    HALR->structured.autoSCSIData.fMultiBoot                   = safe ? 0 : 1;
    HALR->structured.autoSCSIData.fRoundRobinScheme            = safe ? 1 : 0; /* 1 = aggressive, 0 = strict */

    HALR->structured.autoSCSIData.uHostAdapterIoPortAddress = 2; /* 0 = primary (330h), 1 = secondary (334h), 2 = disable, 3 = reserved */
}

static void
BuslogicInitializeAutoSCSIRam(x54x_t *dev)
{
    buslogic_data_t  *bl   = (buslogic_data_t *) dev->ven_data;
    const HALocalRAM *HALR = &bl->LocalRAM;

    FILE *fp;

    fp = nvr_fopen(BuslogicGetNVRFileName(bl), "rb");
    if (fp) {
        if (fread(&(bl->LocalRAM.structured.autoSCSIData), 1, 64, fp) != 64)
            fatal("BuslogicInitializeAutoSCSIRam(): Error reading data\n");
        fclose(fp);
        fp = NULL;
        if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
            x54x_io_remove(dev, dev->Base, 4);
            switch (HALR->structured.autoSCSIData.uHostAdapterIoPortAddress) {
                case 0:
                    dev->Base = 0x330;
                    break;
                case 1:
                    dev->Base = 0x334;
                    break;
                default:
                    dev->Base = 0;
                    break;
            }
            x54x_io_set(dev, dev->Base, 4);
        }
    } else {
        BuslogicAutoSCSIRamSetDefaults(dev, 0);
    }
}

static void
buslogic_cmd_phase1(void *priv)
{
    x54x_t *dev = (x54x_t *) priv;

    if ((dev->CmdParam == 2) && (dev->Command == 0x90)) {
        dev->CmdParamLeft = dev->CmdBuf[1];
    }

    if ((dev->CmdParam == 10) && ((dev->Command == 0x97) || (dev->Command == 0xA7))) {
        dev->CmdParamLeft = dev->CmdBuf[6];
        dev->CmdParamLeft <<= 8;
        dev->CmdParamLeft |= dev->CmdBuf[7];
        dev->CmdParamLeft <<= 8;
        dev->CmdParamLeft |= dev->CmdBuf[8];
    }

    if ((dev->CmdParam == 4) && (dev->Command == 0xA9)) {
        dev->CmdParamLeft = dev->CmdBuf[3];
        dev->CmdParamLeft <<= 8;
        dev->CmdParamLeft |= dev->CmdBuf[2];
    }
}

static uint8_t
buslogic_get_host_id(void *priv)
{
    x54x_t                *dev = (x54x_t *) priv;
    const buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    const HALocalRAM *HALR = &bl->LocalRAM;

    if ((bl->chip == CHIP_BUSLOGIC_ISA_542B_1991_12_14) || (bl->chip == CHIP_BUSLOGIC_ISA_545S_1992_10_05) || (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_VLB_445S_1993_11_16))
        return dev->HostID;
    else
        return HALR->structured.autoSCSIData.uSCSIId;
}

static uint8_t
buslogic_get_irq(void *priv)
{
    x54x_t                *dev = (x54x_t *) priv;
    const buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    uint8_t bl_irq[7] = { 0, 9, 10, 11, 12, 14, 15 };

    const HALocalRAM *HALR = &bl->LocalRAM;

    if ((bl->chip == CHIP_BUSLOGIC_ISA_542B_1991_12_14) || (bl->chip == CHIP_BUSLOGIC_ISA_545S_1992_10_05) || (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_VLB_445S_1993_11_16) || (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30))
        return dev->Irq;
    else
        return bl_irq[HALR->structured.autoSCSIData.uIrqChannel];
}

static uint8_t
buslogic_get_dma(void *priv)
{
    x54x_t                *dev = (x54x_t *) priv;
    const buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    uint8_t bl_dma[4] = { 0, 5, 6, 7 };

    const HALocalRAM *HALR = &bl->LocalRAM;

    if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30)
        return (dev->Base ? 7 : 0);
    else if ((bl->chip == CHIP_BUSLOGIC_ISA_542B_1991_12_14) || (bl->chip == CHIP_BUSLOGIC_ISA_545S_1992_10_05) || (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_VLB_445S_1993_11_16))
        return dev->DmaChannel;
    else
        return bl_dma[HALR->structured.autoSCSIData.uDMAChannel];
}

static uint8_t
buslogic_param_len(void *priv)
{
    x54x_t                *dev = (x54x_t *) priv;
    const buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    switch (dev->Command) {
        case 0x21:
            return 5;
        case 0x25:
        case 0x8B:
        case 0x8C:
        case 0x8D:
        case 0x8F:
        case 0x92:
        case 0x96:
            return 1;
        case 0x81:
            return sizeof(MailboxInitExtended_t);
        case 0x83:
            return 12;
        case 0x90:
        case 0x91:
            return 2;
        case 0x94:
        case 0xFB:
            return 3;
        case 0x93: /* Valid only for VLB */
            return (bl->chip == CHIP_BUSLOGIC_VLB_445C_1994_12_01 || bl->chip == CHIP_BUSLOGIC_VLB_445S_1993_11_16) ? 1 : 0;
        case 0x95: /* Valid only for PCI */
            return (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 1 : 0;
        case 0x97: /* Valid only for PCI */
        case 0xA7: /* Valid only for PCI */
            return (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 10 : 0;
        case 0xA8: /* Valid only for PCI */
        case 0xA9: /* Valid only for PCI */
            return (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 4 : 0;
        default:
            return 0;
    }
}

static void
BuslogicSCSIBIOSDMATransfer(x54x_t *dev, ESCMD *ESCSICmd, uint8_t TargetID, int dir, int transfer_size)
{
    uint32_t       DataPointer = ESCSICmd->DataPointer;
    int            DataLength  = ESCSICmd->DataLength;
    uint32_t       Address;
    uint32_t       TransferLength;
    scsi_device_t *sd = &scsi_devices[dev->bus][TargetID];

    if (ESCSICmd->DataDirection == 0x03) {
        /* Non-data command. */
        buslogic_log("BuslogicSCSIBIOSDMATransfer(): Non-data control byte\n");
        return;
    }

    buslogic_log("BuslogicSCSIBIOSDMATransfer(): BIOS Data Buffer read: length %d, pointer 0x%04X\n", DataLength, DataPointer);

    /* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
       checking its length, so do this procedure for both read/write commands. */
    if ((DataLength > 0) && (sd->buffer_length > 0)) {
        Address        = DataPointer;
        TransferLength = MIN(DataLength, sd->buffer_length);

        if (dir && ((ESCSICmd->DataDirection == CCB_DATA_XFER_OUT) || (ESCSICmd->DataDirection == 0x00))) {
            buslogic_log("BusLogic BIOS DMA: Reading %i bytes from %08X\n", TransferLength, Address);
            dma_bm_read(Address, sd->sc->temp_buffer, TransferLength, transfer_size);
        } else if (!dir && ((ESCSICmd->DataDirection == CCB_DATA_XFER_IN) || (ESCSICmd->DataDirection == 0x00))) {
            buslogic_log("BusLogic BIOS DMA: Writing %i bytes at %08X\n", TransferLength, Address);
            dma_bm_write(Address, sd->sc->temp_buffer, TransferLength, transfer_size);
        }
    }
}

static void
BuslogicSCSIBIOSRequestSetup(x54x_t *dev, uint8_t *CmdBuf, uint8_t *DataInBuf, uint8_t DataReply)
{
    ESCMD   *ESCSICmd = (ESCMD *) CmdBuf;
    uint8_t  temp_cdb[12];
    int      target_cdb_len = 12;
#ifdef ENABLE_BUSLOGIC_LOG
    uint8_t target_id = 0;
#endif
    int            phase;
    scsi_device_t *sd = &scsi_devices[dev->bus][ESCSICmd->TargetId];

    DataInBuf[0] = DataInBuf[1] = 0;

    if ((ESCSICmd->TargetId > 15) || (ESCSICmd->LogicalUnit > 7)) {
        DataInBuf[2] = CCB_INVALID_CCB;
        DataInBuf[3] = SCSI_STATUS_OK;
        return;
    }

    buslogic_log("Scanning SCSI Target ID %i\n", ESCSICmd->TargetId);

    sd->status = SCSI_STATUS_OK;

    if (!scsi_device_present(sd) || (ESCSICmd->LogicalUnit > 0)) {
        buslogic_log("SCSI Target ID %i has no device attached\n", ESCSICmd->TargetId);
        DataInBuf[2] = CCB_SELECTION_TIMEOUT;
        DataInBuf[3] = SCSI_STATUS_OK;
        return;
    } else {
        buslogic_log("SCSI Target ID %i detected and working\n", ESCSICmd->TargetId);
        scsi_device_identify(sd, ESCSICmd->LogicalUnit);

        buslogic_log("Transfer Control %02X\n", ESCSICmd->DataDirection);
        buslogic_log("CDB Length %i\n", ESCSICmd->CDBLength);
    }

    target_cdb_len = 12;

    if (!scsi_device_valid(sd))
        fatal("SCSI target on ID %02i has disappeared\n", ESCSICmd->TargetId);

    buslogic_log("SCSI target command being executed on: SCSI ID %i, SCSI LUN %i, Target %i\n", ESCSICmd->TargetId, ESCSICmd->LogicalUnit, target_id);

    buslogic_log("SCSI Cdb[0]=0x%02X\n", ESCSICmd->CDB[0]);
    for (uint8_t i = 1; i < ESCSICmd->CDBLength; i++) {
        buslogic_log("SCSI Cdb[%i]=%i\n", i, ESCSICmd->CDB[i]);
    }

    memset(temp_cdb, 0, target_cdb_len);
    if (ESCSICmd->CDBLength <= target_cdb_len) {
        memcpy(temp_cdb, ESCSICmd->CDB, ESCSICmd->CDBLength);
    } else {
        memcpy(temp_cdb, ESCSICmd->CDB, target_cdb_len);
    }

    sd->buffer_length = ESCSICmd->DataLength;

    scsi_device_command_phase0(sd, temp_cdb);

    phase = sd->phase;
    if (phase != SCSI_PHASE_STATUS) {
        BuslogicSCSIBIOSDMATransfer(dev, ESCSICmd, ESCSICmd->TargetId, (phase == SCSI_PHASE_DATA_OUT), dev->transfer_size);
        scsi_device_command_phase1(sd);
    }

    buslogic_log("BIOS Request complete\n");
    scsi_device_identify(sd, SCSI_LUN_USE_CDB);

    if (sd->status == SCSI_STATUS_OK) {
        DataInBuf[2] = CCB_COMPLETE;
        DataInBuf[3] = SCSI_STATUS_OK;
    } else if (scsi_devices[dev->bus][ESCSICmd->TargetId].status == SCSI_STATUS_CHECK_CONDITION) {
        DataInBuf[2] = CCB_COMPLETE;
        DataInBuf[3] = SCSI_STATUS_CHECK_CONDITION;
    }

    dev->DataReplyLeft = DataReply;
}

static uint8_t
buslogic_cmds(void *priv)
{
    x54x_t          *dev = (x54x_t *) priv;
    buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    const HALocalRAM *HALR = &bl->LocalRAM;

    FILE                                 *fp;
    uint16_t                              TargetsPresentMask = 0;
    uint32_t                              Offset;
    int                                   i = 0;
    const MailboxInitExtended_t          *MailboxInitE;
    ReplyInquireExtendedSetupInformation *ReplyIESI;
    BuslogicPCIInformation_t             *ReplyPI;
    int                                   cCharsToTransfer;

    buslogic_log("Buslogic cmds = 0x%02x\n", dev->Command);

    switch (dev->Command) {
        case 0x20:
            dev->DataReplyLeft = 0;
            x54x_reset_ctrl(dev, 1);
            break;
        case 0x21:
            if (dev->CmdParam == 1)
                dev->CmdParamLeft = dev->CmdBuf[0];
            dev->DataReplyLeft = 0;
            break;
        case 0x23:
            memset(dev->DataBuf, 0, 8);
            for (i = 8; i < 15; i++) {
                dev->DataBuf[i - 8] = 0;
                if (scsi_device_present(&scsi_devices[dev->bus][i]) && (i != buslogic_get_host_id(dev)))
                    dev->DataBuf[i - 8] |= 1;
            }
            dev->DataReplyLeft = 8;
            break;
        case 0x24:
            for (i = 0; i < 15; i++) {
                if (scsi_device_present(&scsi_devices[dev->bus][i]) && (i != buslogic_get_host_id(dev)))
                    TargetsPresentMask |= (1 << i);
            }
            dev->DataBuf[0]    = TargetsPresentMask & 0xFF;
            dev->DataBuf[1]    = TargetsPresentMask >> 8;
            dev->DataReplyLeft = 2;
            break;
        case 0x25:
            if (dev->CmdBuf[0] == 0)
                dev->IrqEnabled = 0;
            else
                dev->IrqEnabled = 1;
            return 1;
        case 0x81:
            dev->flags &= ~X54X_MBX_24BIT;

            MailboxInitE = (MailboxInitExtended_t *) dev->CmdBuf;

            dev->MailboxInit    = 1;
            dev->MailboxCount   = MailboxInitE->Count;
            dev->MailboxOutAddr = MailboxInitE->Address;
            dev->MailboxInAddr  = MailboxInitE->Address + (dev->MailboxCount * sizeof(Mailbox32_t));

            buslogic_log("Buslogic Extended Initialize Mailbox Command\n");
            buslogic_log("Mailbox Out Address=0x%08X\n", dev->MailboxOutAddr);
            buslogic_log("Mailbox In Address=0x%08X\n", dev->MailboxInAddr);
            buslogic_log("Initialized Extended Mailbox, %d entries at 0x%08X\n", MailboxInitE->Count, MailboxInitE->Address);

            dev->Status &= ~STAT_INIT;
            dev->DataReplyLeft = 0;
            break;
        case 0x83:
            if (dev->CmdParam == 12) {
                dev->CmdParamLeft = dev->CmdBuf[11];
                buslogic_log("Execute SCSI BIOS Command: %u more bytes follow\n", dev->CmdParamLeft);
            } else {
                buslogic_log("Execute SCSI BIOS Command: received %u bytes\n", dev->CmdBuf[0]);
                BuslogicSCSIBIOSRequestSetup(dev, dev->CmdBuf, dev->DataBuf, 4);
            }
            break;
        case 0x84:
            dev->DataBuf[0]    = dev->fw_rev[4];
            dev->DataReplyLeft = 1;
            break;
        case 0x85:
            if (strlen(dev->fw_rev) == 6)
                dev->DataBuf[0] = dev->fw_rev[5];
            else
                dev->DataBuf[0] = ' ';
            dev->DataReplyLeft = 1;
            break;
        case 0x86:
            if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
                ReplyPI = (BuslogicPCIInformation_t *) dev->DataBuf;
                memset(ReplyPI, 0, sizeof(BuslogicPCIInformation_t));
                ReplyPI->InformationIsValid = 0;
                switch (dev->Base) {
                    case 0x330:
                        ReplyPI->IsaIOPort = 0;
                        break;
                    case 0x334:
                        ReplyPI->IsaIOPort = 1;
                        break;
                    case 0x230:
                        ReplyPI->IsaIOPort = 2;
                        break;
                    case 0x234:
                        ReplyPI->IsaIOPort = 3;
                        break;
                    case 0x130:
                        ReplyPI->IsaIOPort = 4;
                        break;
                    case 0x134:
                        ReplyPI->IsaIOPort = 5;
                        break;
                    default:
                        ReplyPI->IsaIOPort = 6;
                        break;
                }
                ReplyPI->IRQ       = dev->Irq;
                dev->DataReplyLeft = sizeof(BuslogicPCIInformation_t);
            } else {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
            }
            break;
        case 0x8B:
            /* The reply length is set by the guest and is found in the first byte of the command buffer. */
            dev->DataReplyLeft = dev->CmdBuf[0];
            memset(dev->DataBuf, 0, dev->DataReplyLeft);
            if (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23)
                i = 5;
            else
                i = 4;
            cCharsToTransfer = MIN(dev->DataReplyLeft, i);

            memcpy(dev->DataBuf, &(bl->LocalRAM.structured.autoSCSIData.aHostAdaptertype[1]), cCharsToTransfer);
            break;
        case 0x8C:
            dev->DataReplyLeft = dev->CmdBuf[0];
            memset(dev->DataBuf, 0, dev->DataReplyLeft);
            break;
        case 0x8D:
            dev->DataReplyLeft = dev->CmdBuf[0];
            ReplyIESI          = (ReplyInquireExtendedSetupInformation *) dev->DataBuf;
            memset(ReplyIESI, 0, sizeof(ReplyInquireExtendedSetupInformation));

            switch (bl->chip) {
                case CHIP_BUSLOGIC_ISA_542B_1991_12_14:
                case CHIP_BUSLOGIC_ISA_545S_1992_10_05:
                case CHIP_BUSLOGIC_ISA_542BH_1993_05_23:
                case CHIP_BUSLOGIC_ISA_545C_1994_12_01:
                case CHIP_BUSLOGIC_VLB_445S_1993_11_16:
                case CHIP_BUSLOGIC_VLB_445C_1994_12_01:
                    ReplyIESI->uBusType = 'A'; /* ISA style */
                    break;
                case CHIP_BUSLOGIC_MCA_640A_1993_05_23:
                    ReplyIESI->uBusType = 'M'; /* MCA style */
                    break;
                case CHIP_BUSLOGIC_PCI_958D_1995_12_30:
                    ReplyIESI->uBusType = 'E'; /* PCI style */
                    break;

                default:
                    break;
            }
            ReplyIESI->uBiosAddress          = 0xd8;
            ReplyIESI->u16ScatterGatherLimit = 8192;
            ReplyIESI->cMailbox              = dev->MailboxCount;
            ReplyIESI->uMailboxAddressBase   = dev->MailboxOutAddr;
            ReplyIESI->fHostWideSCSI         = (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) ? 1 : 0;
            if ((bl->chip != CHIP_BUSLOGIC_ISA_542B_1991_12_14) && (bl->chip != CHIP_BUSLOGIC_ISA_545S_1992_10_05) && (bl->chip != CHIP_BUSLOGIC_ISA_542BH_1993_05_23) && (bl->chip != CHIP_BUSLOGIC_MCA_640A_1993_05_23) && (bl->chip != CHIP_BUSLOGIC_VLB_445S_1993_11_16))
                ReplyIESI->fLevelSensitiveInterrupt = bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt;
            if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30)
                ReplyIESI->fHostUltraSCSI = 1;
            memcpy(ReplyIESI->aFirmwareRevision, &(dev->fw_rev[strlen(dev->fw_rev) - 3]), sizeof(ReplyIESI->aFirmwareRevision));
            buslogic_log("Return Extended Setup Information: %d\n", dev->CmdBuf[0]);
            break;
        case 0x8F:
            bl->fAggressiveRoundRobinMode = dev->CmdBuf[0] & 1;
            buslogic_log("Aggressive Round Robin Mode = %d\n", bl->fAggressiveRoundRobinMode);
            dev->DataReplyLeft = 0;
            break;
        case 0x90:
            buslogic_log("Store Local RAM\n");
            Offset             = dev->CmdBuf[0];
            dev->DataReplyLeft = 0;
            memcpy(&(bl->LocalRAM.u8View[Offset]), &(dev->CmdBuf[2]), dev->CmdBuf[1]);

            dev->DataReply = 0;
            break;
        case 0x91:
            buslogic_log("Fetch Local RAM\n");
            Offset             = dev->CmdBuf[0];
            dev->DataReplyLeft = dev->CmdBuf[1];
            memcpy(dev->DataBuf, &(bl->LocalRAM.u8View[Offset]), dev->CmdBuf[1]);

            dev->DataReply = 0;
            break;
        case 0x93:
            if ((bl->chip != CHIP_BUSLOGIC_VLB_445C_1994_12_01) && (bl->chip != CHIP_BUSLOGIC_VLB_445S_1993_11_16)) {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
                break;
            }
            fallthrough;
        case 0x92:
            if ((bl->chip == CHIP_BUSLOGIC_ISA_542B_1991_12_14) || (bl->chip == CHIP_BUSLOGIC_ISA_545S_1992_10_05) || (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_MCA_640A_1993_05_23)) {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
                break;
            }

            dev->DataReplyLeft = 0;

            switch (dev->CmdBuf[0]) {
                case 0:
                case 2:
                    BuslogicAutoSCSIRamSetDefaults(dev, 0);
                    break;
                case 3:
                    BuslogicAutoSCSIRamSetDefaults(dev, 3);
                    break;
                case 1:
                    fp = nvr_fopen(BuslogicGetNVRFileName(bl), "wb");
                    if (fp) {
                        fwrite(&(bl->LocalRAM.structured.autoSCSIData), 1, 64, fp);
                        fclose(fp);
                        fp = NULL;
                    }
                    break;
                default:
                    dev->Status |= STAT_INVCMD;
                    break;
            }

            if ((bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) && !(dev->Status & STAT_INVCMD)) {
                x54x_io_remove(dev, dev->Base, 4);
                switch (HALR->structured.autoSCSIData.uHostAdapterIoPortAddress) {
                    case 0:
                        dev->Base = 0x330;
                        break;
                    case 1:
                        dev->Base = 0x334;
                        break;
                    default:
                        dev->Base = 0;
                        break;
                }
                x54x_io_set(dev, dev->Base, 4);
            }
            break;
        case 0x94:
            if ((bl->chip == CHIP_BUSLOGIC_ISA_542B_1991_12_14) || (bl->chip == CHIP_BUSLOGIC_ISA_545S_1992_10_05) || (bl->chip == CHIP_BUSLOGIC_MCA_640A_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23)) {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
                break;
            }

            if (dev->CmdBuf[0]) {
                buslogic_log("Invalid AutoSCSI command mode %x\n", dev->CmdBuf[0]);
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
            } else {
                dev->DataReplyLeft = dev->CmdBuf[2];
                dev->DataReplyLeft <<= 8;
                dev->DataReplyLeft |= dev->CmdBuf[1];
                memcpy(dev->DataBuf, bl->AutoSCSIROM, dev->DataReplyLeft);
                buslogic_log("Returning AutoSCSI ROM (%04X %04X %04X %04X)\n", dev->DataBuf[0], dev->DataBuf[1], dev->DataBuf[2], dev->DataBuf[3]);
            }
            break;
        case 0x95:
            if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
                if (dev->Base != 0)
                    x54x_io_remove(dev, dev->Base, 4);
                if (dev->CmdBuf[0] < 6) {
                    dev->Base = ((3 - (dev->CmdBuf[0] >> 1)) << 8) | ((dev->CmdBuf[0] & 1) ? 0x34 : 0x30);
                    x54x_io_set(dev, dev->Base, 4);
                } else
                    dev->Base = 0;
                dev->DataReplyLeft = 0;
                return 1;
            } else {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
            }
            break;
        case 0x96:
            if (dev->CmdBuf[0] == 0)
                bl->ExtendedLUNCCBFormat = 0;
            else if (dev->CmdBuf[0] == 1)
                bl->ExtendedLUNCCBFormat = 1;

            dev->DataReplyLeft = 0;
            break;
        case 0x97:
        case 0xA7:
            /* TODO: Actually correctly implement this whole SCSI BIOS Flash stuff. */
            dev->DataReplyLeft = 0;
            break;
        case 0xA8:
            if (bl->chip != CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
                break;
            }

            Offset = dev->CmdBuf[1];
            Offset <<= 8;
            Offset |= dev->CmdBuf[0];

            dev->DataReplyLeft = dev->CmdBuf[3];
            dev->DataReplyLeft <<= 8;
            dev->DataReplyLeft |= dev->CmdBuf[2];

            memcpy(dev->DataBuf, &(bl->SCAMData[Offset]), dev->DataReplyLeft);

            dev->DataReply = 0;
            break;
        case 0xA9:
            if (bl->chip != CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
                dev->DataReplyLeft = 0;
                dev->Status |= STAT_INVCMD;
                break;
            }

            Offset = dev->CmdBuf[1];
            Offset <<= 8;
            Offset |= dev->CmdBuf[0];

            dev->DataReplyLeft = dev->CmdBuf[3];
            dev->DataReplyLeft <<= 8;
            dev->DataReplyLeft |= dev->CmdBuf[2];

            memcpy(&(bl->SCAMData[Offset]), &(dev->CmdBuf[4]), dev->DataReplyLeft);
            dev->DataReplyLeft = 0;

            dev->DataReply = 0;
            break;
        case 0xFB:
            dev->DataReplyLeft = dev->CmdBuf[2];
            break;
        default:
            dev->DataReplyLeft = 0;
            dev->Status |= STAT_INVCMD;
            break;
    }
    return 0;
}

static void
buslogic_setup_data(void *priv)
{
    x54x_t                       *dev = (x54x_t *) priv;
    ReplyInquireSetupInformation *ReplyISI;
    buslogic_setup_t             *bl_setup;
    const buslogic_data_t        *bl   = (buslogic_data_t *) dev->ven_data;
    const HALocalRAM             *HALR = &bl->LocalRAM;

    ReplyISI = (ReplyInquireSetupInformation *) dev->DataBuf;
    bl_setup = (buslogic_setup_t *) ReplyISI->VendorSpecificData;

    ReplyISI->fSynchronousInitiationEnabled = HALR->structured.autoSCSIData.u16SynchronousPermittedMask ? 1 : 0;
    ReplyISI->fParityCheckingEnabled        = (HALR->structured.autoSCSIData.uSCSIConfiguration & 2) ? 1 : 0;

    bl_setup->uSignature = 'B';
    /* The 'D' signature prevents Buslogic's OS/2 drivers from getting too
     * friendly with Adaptec hardware and upsetting the HBA state.
     */
    bl_setup->uCharacterD = 'D'; /* BusLogic model. */
    switch (bl->chip) {
        case CHIP_BUSLOGIC_ISA_542B_1991_12_14:
        case CHIP_BUSLOGIC_ISA_545S_1992_10_05:
        case CHIP_BUSLOGIC_ISA_542BH_1993_05_23:
        case CHIP_BUSLOGIC_ISA_545C_1994_12_01:
            bl_setup->uHostBusType = 'A';
            break;
        case CHIP_BUSLOGIC_MCA_640A_1993_05_23:
            bl_setup->uHostBusType = 'B';
            break;
        case CHIP_BUSLOGIC_VLB_445S_1993_11_16:
        case CHIP_BUSLOGIC_VLB_445C_1994_12_01:
            bl_setup->uHostBusType = 'E';
            break;
        case CHIP_BUSLOGIC_PCI_958D_1995_12_30:
            bl_setup->uHostBusType = 'F';
            break;

        default:
            break;
    }
}

static uint8_t
buslogic_is_aggressive_mode(void *priv)
{
    x54x_t                *dev = (x54x_t *) priv;
    const buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    buslogic_log("Buslogic: Aggressive mode = %d\n", bl->fAggressiveRoundRobinMode);

    return bl->fAggressiveRoundRobinMode;
}

static uint8_t
buslogic_interrupt_type(void *priv)
{
    x54x_t                *dev = (x54x_t *) priv;
    const buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    if ((bl->chip == CHIP_BUSLOGIC_ISA_542B_1991_12_14) || (bl->chip == CHIP_BUSLOGIC_ISA_545S_1992_10_05) || (bl->chip == CHIP_BUSLOGIC_ISA_542BH_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_VLB_445S_1993_11_16) || (bl->chip == CHIP_BUSLOGIC_MCA_640A_1993_05_23))
        return 0;
    else
        return !!bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt;
}

static void
buslogic_reset(void *priv)
{
    x54x_t          *dev = (x54x_t *) priv;
    buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    bl->ExtendedLUNCCBFormat = 0;
}

uint8_t buslogic_pci_regs[256];
bar_t   buslogic_pci_bar[3];

static void
BuslogicBIOSUpdate(buslogic_data_t *bl)
{
    int bios_enabled = buslogic_pci_bar[2].addr_regs[0] & 0x01;

    if (!bl->has_bios) {
        return;
    }

    /* PCI BIOS stuff, just enable_disable. */
    if ((bl->bios_addr > 0) && bios_enabled) {
        mem_mapping_enable(&bl->bios.mapping);
        mem_mapping_set_addr(&bl->bios.mapping,
                             bl->bios_addr, bl->bios_size);
        buslogic_log("BT-958D: BIOS now at: %06X\n", bl->bios_addr);
    } else {
        buslogic_log("BT-958D: BIOS disabled\n");
        mem_mapping_disable(&bl->bios.mapping);
    }
}

static uint8_t
BuslogicPCIRead(UNUSED(int func), int addr, void *priv)
{
    const x54x_t *dev = (x54x_t *) priv;
#ifdef ENABLE_BUSLOGIC_LOG
    buslogic_data_t *bl = (buslogic_data_t *) dev->ven_data;
#endif

    buslogic_log("BT-958D: Reading register %02X\n", addr & 0xff);

    switch (addr) {
        case 0x00:
            return 0x4b;
        case 0x01:
            return 0x10;
        case 0x02:
            return 0x40;
        case 0x03:
            return 0x10;
        case 0x04:
            return buslogic_pci_regs[0x04] & 0x03; /*Respond to IO and memory accesses*/
        case 0x05:
            return 0;
        case 0x07:
            return 2;
        case 0x08:
            return 1; /*Revision ID*/
        case 0x09:
            return 0; /*Programming interface*/
        case 0x0A:
            return 0; /*Subclass*/
        case 0x0B:
            return 1; /*Class code*/
        case 0x0E:
            return 0; /*Header type */
        case 0x10:
            return (buslogic_pci_bar[0].addr_regs[0] & 0xe0) | 1; /*I/O space*/
        case 0x11:
            return buslogic_pci_bar[0].addr_regs[1];
        case 0x12:
            return buslogic_pci_bar[0].addr_regs[2];
        case 0x13:
            return buslogic_pci_bar[0].addr_regs[3];
        case 0x14:
#if 0
            return (buslogic_pci_bar[1].addr_regs[0] & 0xe0); /*Memory space*/
#endif
            return 0x00;
        case 0x15:
            return buslogic_pci_bar[1].addr_regs[1] & 0xc0;
        case 0x16:
            return buslogic_pci_bar[1].addr_regs[2];
        case 0x17:
            return buslogic_pci_bar[1].addr_regs[3];
        case 0x2C:
            return 0x4b;
        case 0x2D:
            return 0x10;
        case 0x2E:
            return 0x40;
        case 0x2F:
            return 0x10;
        case 0x30: /* PCI_ROMBAR */
            buslogic_log("BT-958D: BIOS BAR 00 = %02X\n", buslogic_pci_bar[2].addr_regs[0] & 0x01);
            return buslogic_pci_bar[2].addr_regs[0] & 0x01;
        case 0x31: /* PCI_ROMBAR 15:11 */
            buslogic_log("BT-958D: BIOS BAR 01 = %02X\n", (buslogic_pci_bar[2].addr_regs[1] & bl->bios_mask));
            return buslogic_pci_bar[2].addr_regs[1];
        case 0x32: /* PCI_ROMBAR 23:16 */
            buslogic_log("BT-958D: BIOS BAR 02 = %02X\n", buslogic_pci_bar[2].addr_regs[2]);
            return buslogic_pci_bar[2].addr_regs[2];
        case 0x33: /* PCI_ROMBAR 31:24 */
            buslogic_log("BT-958D: BIOS BAR 03 = %02X\n", buslogic_pci_bar[2].addr_regs[3]);
            return buslogic_pci_bar[2].addr_regs[3];
        case 0x3C:
            return dev->Irq;
        case 0x3D:
            return PCI_INTA;

        default:
            break;
    }

    return 0;
}

static void
BuslogicPCIWrite(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    x54x_t          *dev = (x54x_t *) priv;
    buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    uint8_t valxor;

    buslogic_log("BT-958D: Write value %02X to register %02X\n", val, addr & 0xff);

    switch (addr) {
        case 0x04:
            valxor = (val & 0x27) ^ buslogic_pci_regs[addr];
            if (valxor & PCI_COMMAND_IO) {
                x54x_io_remove(dev, bl->PCIBase, 32);
                if ((bl->PCIBase != 0) && (val & PCI_COMMAND_IO)) {
                    x54x_io_set(dev, bl->PCIBase, 32);
                }
            }
            if (valxor & PCI_COMMAND_MEM) {
                x54x_mem_disable(dev);
                if ((bl->MMIOBase != 0) && (val & PCI_COMMAND_MEM)) {
                    x54x_mem_set_addr(dev, bl->MMIOBase);
                }
            }
            buslogic_pci_regs[addr] = val & 0x27;
            break;

        case 0x10:
            val &= 0xe0;
            val |= 1;
            fallthrough;

        case 0x11:
        case 0x12:
        case 0x13:
            /* I/O Base set. */
            /* First, remove the old I/O. */
            x54x_io_remove(dev, bl->PCIBase, 32);
            /* Then let's set the PCI regs. */
            buslogic_pci_bar[0].addr_regs[addr & 3] = val;
            /* Then let's calculate the new I/O base. */
            bl->PCIBase = buslogic_pci_bar[0].addr & 0xffe0;
            /* Log the new base. */
            buslogic_log("BusLogic PCI: New I/O base is %04X\n", bl->PCIBase);
            /* We're done, so get out of the here. */
            if (buslogic_pci_regs[4] & PCI_COMMAND_IO) {
                if (bl->PCIBase != 0) {
                    x54x_io_set(dev, bl->PCIBase, 32);
                }
            }
            return;

        case 0x14:
            val &= 0xe0;
            fallthrough;

        case 0x15:
        case 0x16:
        case 0x17:
            /* MMIO Base set. */
            /* First, remove the old I/O. */
            x54x_mem_disable(dev);
            /* Then let's set the PCI regs. */
            buslogic_pci_bar[1].addr_regs[addr & 3] = val;
            /* Then let's calculate the new I/O base. */
#if 0
            bl->MMIOBase = buslogic_pci_bar[1].addr & 0xffffffe0;
#endif
            /* Give it a 4 kB alignment as that's this emulator's granularity. */
            buslogic_pci_bar[1].addr &= 0xffffc000;
            bl->MMIOBase = buslogic_pci_bar[1].addr & 0xffffc000;
            /* Log the new base. */
            buslogic_log("BusLogic PCI: New MMIO base is %04X\n", bl->MMIOBase);
            /* We're done, so get out of the here. */
            if (buslogic_pci_regs[4] & PCI_COMMAND_MEM) {
                if (bl->MMIOBase != 0) {
                    x54x_mem_set_addr(dev, bl->MMIOBase);
                }
            }
            return;

        case 0x30: /* PCI_ROMBAR */
        case 0x31: /* PCI_ROMBAR */
        case 0x32: /* PCI_ROMBAR */
        case 0x33: /* PCI_ROMBAR */
            buslogic_pci_bar[2].addr_regs[addr & 3] = val;
            buslogic_pci_bar[2].addr &= 0xffffc001;
            bl->bios_addr = buslogic_pci_bar[2].addr & 0xffffc000;
            buslogic_log("BT-958D: BIOS BAR %02X = NOW %02X (%02X)\n", addr & 3, buslogic_pci_bar[2].addr_regs[addr & 3], val);
            BuslogicBIOSUpdate(bl);
            return;

        case 0x3C:
            buslogic_pci_regs[addr] = val;
            if (val != 0xFF) {
                buslogic_log("BusLogic IRQ now: %i\n", val);
                dev->Irq = val;
            } else
                dev->Irq = 0;
            return;

        default:
            break;
    }
}

static void
BuslogicInitializeLocalRAM(buslogic_data_t *bl)
{
    memset(bl->LocalRAM.u8View, 0, sizeof(HALocalRAM));
    if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
        bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt = 1;
    } else {
        bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt = 0;
    }

    bl->LocalRAM.structured.autoSCSIData.u16DeviceEnabledMask        = ~0;
    bl->LocalRAM.structured.autoSCSIData.u16WidePermittedMask        = ~0;
    bl->LocalRAM.structured.autoSCSIData.u16FastPermittedMask        = ~0;
    bl->LocalRAM.structured.autoSCSIData.u16SynchronousPermittedMask = ~0;
    bl->LocalRAM.structured.autoSCSIData.u16DisconnectPermittedMask  = ~0;
    bl->LocalRAM.structured.autoSCSIData.fRoundRobinScheme           = 0;
    bl->LocalRAM.structured.autoSCSIData.u16UltraPermittedMask       = ~0;
}

static uint8_t
buslogic_mca_read(int port, void *priv)
{
    const x54x_t *dev = (x54x_t *) priv;

    return (dev->pos_regs[port & 7]);
}

static void
buslogic_mca_write(int port, uint8_t val, void *priv)
{
    x54x_t          *dev = (x54x_t *) priv;
    buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    HALocalRAM *HALR = &bl->LocalRAM;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /* This is always necessary so that the old handler doesn't remain. */
    x54x_io_remove(dev, dev->Base, 4);

    /* Get the new assigned I/O base address. */
    if (dev->pos_regs[3]) {
        dev->Base = dev->pos_regs[3] << 8;
        dev->Base |= ((dev->pos_regs[2] & 0x10) ? 0x34 : 0x30);
    } else {
        dev->Base = 0x0000;
    }

    /* Save the new IRQ and DMA channel values. */
    dev->Irq        = ((dev->pos_regs[2] >> 1) & 0x07) + 8;
    dev->DmaChannel = dev->pos_regs[5] & 0x0f;

    /* Extract the BIOS ROM address info. */
    if (dev->pos_regs[2] & 0xe0)
        switch (dev->pos_regs[2] & 0xe0) {
            case 0xe0: /* [0]=111x xxxx */
                bl->bios_addr = 0xDC000;
                break;

            case 0x00: /* [0]=000x xxxx */
                bl->bios_addr = 0;
                break;

            case 0xc0: /* [0]=110x xxxx */
                bl->bios_addr = 0xD8000;
                break;

            case 0xa0: /* [0]=101x xxxx */
                bl->bios_addr = 0xD4000;
                break;

            case 0x80: /* [0]=100x xxxx */
                bl->bios_addr = 0xD0000;
                break;

            case 0x60: /* [0]=011x xxxx */
                bl->bios_addr = 0xCC000;
                break;

            case 0x40: /* [0]=010x xxxx */
                bl->bios_addr = 0xC8000;
                break;

            case 0x20: /* [0]=001x xxxx */
                bl->bios_addr = 0xC4000;
                break;

            default:
                break;
        }
    else {
        /* Disabled. */
        bl->bios_addr = 0x000000;
    }

    /*
     * Get misc SCSI config stuff.  For now, we are only
     * interested in the configured HA target ID:
     *
     *  pos[2]=111xxxxx = 7
     *  pos[2]=000xxxxx = 0
     */
    dev->HostID                           = (dev->pos_regs[4] >> 5) & 0x07;
    HALR->structured.autoSCSIData.uSCSIId = dev->HostID;

    /*
     * SYNC mode is pos[2]=xxxxxx1x.
     *
     * SCSI Parity is pos[2]=xxx1xxxx.
     *
     * DOS Disk Space > 1GBytes is pos[2] = xxxx1xxx.
     */
    /* Parity. */
    HALR->structured.autoSCSIData.uSCSIConfiguration &= ~2;
    HALR->structured.autoSCSIData.uSCSIConfiguration |= (dev->pos_regs[4] & 2);

    /* Sync. */
    HALR->structured.autoSCSIData.u16SynchronousPermittedMask = (dev->pos_regs[4] & 0x10) ? 0xffff : 0x0000;

    /* DOS Disk Space > 1GBytes */
    HALR->structured.autoSCSIData.uBIOSConfiguration &= ~4;
    HALR->structured.autoSCSIData.uBIOSConfiguration |= (dev->pos_regs[4] & 8) ? 4 : 0;

    switch (dev->DmaChannel) {
        case 5:
            HALR->structured.autoSCSIData.uDMAChannel = 1;
            break;
        case 6:
            HALR->structured.autoSCSIData.uDMAChannel = 2;
            break;
        case 7:
            HALR->structured.autoSCSIData.uDMAChannel = 3;
            break;
        default:
            HALR->structured.autoSCSIData.uDMAChannel = 0;
            break;
    }

    switch (dev->Irq) {
        case 9:
            HALR->structured.autoSCSIData.uIrqChannel = 1;
            break;
        case 10:
            HALR->structured.autoSCSIData.uIrqChannel = 2;
            break;
        case 11:
            HALR->structured.autoSCSIData.uIrqChannel = 3;
            break;
        case 12:
            HALR->structured.autoSCSIData.uIrqChannel = 4;
            break;
        case 14:
            HALR->structured.autoSCSIData.uIrqChannel = 5;
            break;
        case 15:
            HALR->structured.autoSCSIData.uIrqChannel = 6;
            break;
        default:
            HALR->structured.autoSCSIData.uIrqChannel = 0;
            break;
    }

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */
    mem_mapping_disable(&dev->bios.mapping);

    /* Initialize the device if fully configured. */
    if (dev->pos_regs[2] & 0x01) {
        /* Card enabled; register (new) I/O handler. */
        x54x_io_set(dev, dev->Base, 4);

        /* Reset the device. */
        x54x_reset_ctrl(dev, CTRL_HRST);

        /* Enable or disable the BIOS ROM. */
        if (bl->has_bios && (bl->bios_addr != 0x000000)) {
            mem_mapping_enable(&bl->bios.mapping);
            mem_mapping_set_addr(&bl->bios.mapping, bl->bios_addr, ROM_SIZE);
        }

        /* Say hello. */
        buslogic_log("BT-640A: I/O=%04x, IRQ=%d, DMA=%d, BIOS @%05X, HOST ID %i\n",
                     dev->Base, dev->Irq, dev->DmaChannel, bl->bios_addr, dev->HostID);
    }
}

static uint8_t
buslogic_mca_feedb(void *priv)
{
    const x54x_t *dev = (x54x_t *) priv;

    return (dev->pos_regs[2] & 0x01);
}

void
BuslogicDeviceReset(void *priv)
{
    x54x_t          *dev = (x54x_t *) priv;
    buslogic_data_t *bl  = (buslogic_data_t *) dev->ven_data;

    x54x_device_reset(dev);

    BuslogicInitializeLocalRAM(bl);
    BuslogicInitializeAutoSCSIRam(dev);
}

static void *
buslogic_init(const device_t *info)
{
    x54x_t          *dev;
    const char      *bios_rom_name;
    uint16_t         bios_rom_size = 0;
    uint16_t         bios_rom_mask = 0;
    uint8_t          has_autoscsi_rom;
    const char      *autoscsi_rom_name = NULL;
    uint16_t         autoscsi_rom_size = 0;
    uint8_t          has_scam_rom;
    const char      *scam_rom_name = NULL;
    uint16_t         scam_rom_size = 0;
    FILE            *fp;
    buslogic_data_t *bl;
    uint32_t         bios_rom_addr;

    /* Call common initializer. */
    dev      = x54x_init(info);
    dev->bus = scsi_get_bus();

    dev->ven_data = calloc(1, sizeof(buslogic_data_t));

    bl = (buslogic_data_t *) dev->ven_data;

    dev->card_bus = info->flags;
    if (!(info->flags & DEVICE_MCA) && !(info->flags & DEVICE_PCI)) {
        dev->Base       = device_get_config_hex16("base");
        dev->Irq        = device_get_config_int("irq");
        dev->DmaChannel = device_get_config_int("dma");
    } else if (info->flags & DEVICE_PCI) {
        dev->Base = 0;
    }
    dev->HostID         = 7; /* default HA ID */
    dev->setup_info_len = sizeof(buslogic_setup_t);
    dev->max_id         = 7;
    dev->flags          = X54X_INT_GEOM_WRITABLE;

    bl->chip     = info->local;
    bl->PCIBase  = 0;
    bl->MMIOBase = 0;
    if (info->flags & DEVICE_PCI) {
        bios_rom_addr = 0xd8000;
        bl->has_bios  = device_get_config_int("bios");
    } else if (info->flags & DEVICE_MCA) {
        bios_rom_addr = 0xd8000;
        bl->has_bios  = 1;
    } else {
        bios_rom_addr = device_get_config_hex20("bios_addr");
        bl->has_bios  = !!bios_rom_addr;
    }

    dev->ven_cmd_phase1     = buslogic_cmd_phase1;
    dev->ven_get_host_id    = buslogic_get_host_id;
    dev->ven_get_irq        = buslogic_get_irq;
    dev->ven_get_dma        = buslogic_get_dma;
    dev->get_ven_param_len  = buslogic_param_len;
    dev->ven_cmds           = buslogic_cmds;
    dev->interrupt_type     = buslogic_interrupt_type;
    dev->is_aggressive_mode = buslogic_is_aggressive_mode;
    dev->get_ven_data       = buslogic_setup_data;
    dev->ven_reset          = buslogic_reset;

    strcpy(dev->vendor, "BusLogic");

    bl->fAggressiveRoundRobinMode = 1;

    bios_rom_name    = NULL;
    has_autoscsi_rom = 0;
    has_scam_rom     = 0;

    switch (bl->chip) {
        case CHIP_BUSLOGIC_ISA_542B_1991_12_14: /*Dated December 14th, 1991*/
            strcpy(dev->name, "BT-542B");
            bios_rom_name    = "roms/scsi/buslogic/BT-542B_BIOS.ROM";
            bios_rom_size    = 0x4000;
            bios_rom_mask    = 0x3fff;
            has_autoscsi_rom = 0;
            has_scam_rom     = 0;
            dev->fw_rev      = "AA221";
            dev->ha_bps      = 5000000.0; /* normal SCSI */
            dev->max_id      = 7;         /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_ISA_545S_1992_10_05: /*Dated October 5th, 1992*/
            strcpy(dev->name, "BT-545S");
            bios_rom_name    = "roms/scsi/buslogic/BT-545S_BIOS.rom";
            bios_rom_size    = 0x4000;
            bios_rom_mask    = 0x3fff;
            has_autoscsi_rom = 0;
            has_scam_rom     = 0;
            dev->fw_rev      = "AA331";
            dev->ha_bps      = 5000000.0; /* normal SCSI */
            dev->max_id      = 7;         /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_ISA_542BH_1993_05_23: /*Dated May 23rd, 1993*/
            strcpy(dev->name, "BT-542BH");
            bios_rom_name    = "roms/scsi/buslogic/BT-542BH_BIOS.rom";
            bios_rom_size    = 0x4000;
            bios_rom_mask    = 0x3fff;
            has_autoscsi_rom = 0;
            has_scam_rom     = 0;
            dev->fw_rev      = "AA335";
            dev->ha_bps      = 5000000.0; /* normal SCSI */
            dev->max_id      = 7;         /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_ISA_545C_1994_12_01: /*Dated December 1st, 1994*/
            strcpy(dev->name, "BT-545C");
            bios_rom_name     = "roms/scsi/buslogic/BT-545C_BIOS.rom";
            bios_rom_size     = 0x4000;
            bios_rom_mask     = 0x3fff;
            has_autoscsi_rom  = 1;
            autoscsi_rom_name = "roms/scsi/buslogic/BT-545C_AutoSCSI.rom";
            autoscsi_rom_size = 0x4000;
            has_scam_rom      = 0;
            dev->fw_rev       = "AA425J";
            dev->ha_bps       = 10000000.0; /* fast SCSI */
            dev->max_id       = 7;          /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_MCA_640A_1993_05_23: /*Dated May 23rd, 1993*/
            strcpy(dev->name, "BT-640A");
            bios_rom_name    = "roms/scsi/buslogic/BT-640A_BIOS.rom";
            bios_rom_size    = 0x4000;
            bios_rom_mask    = 0x3fff;
            has_autoscsi_rom = 0;
            has_scam_rom     = 0;
            dev->fw_rev      = "BA335";
            dev->flags |= X54X_32BIT;
            dev->pos_regs[0] = 0x08; /* MCA board ID */
            dev->pos_regs[1] = 0x07;
            mca_add(buslogic_mca_read, buslogic_mca_write, buslogic_mca_feedb, NULL, dev);
            dev->ha_bps = 5000000.0; /* normal SCSI */
            dev->max_id = 7;         /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_VLB_445S_1993_11_16: /*Dated November 16th, 1993*/
            strcpy(dev->name, "BT-445S");
            bios_rom_name    = "roms/scsi/buslogic/BT-445S_BIOS.rom";
            bios_rom_size    = 0x4000;
            bios_rom_mask    = 0x3fff;
            has_autoscsi_rom = 0;
            has_scam_rom     = 0;
            dev->fw_rev      = "AA335";
            dev->flags |= X54X_32BIT;
            dev->ha_bps = 5000000.0; /* normal SCSI */
            dev->max_id = 7;         /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_VLB_445C_1994_12_01: /*Dated December 1st, 1994*/
            strcpy(dev->name, "BT-445C");
            bios_rom_name     = "roms/scsi/buslogic/BT-445C_BIOS.rom";
            bios_rom_size     = 0x4000;
            bios_rom_mask     = 0x3fff;
            has_autoscsi_rom  = 1;
            autoscsi_rom_name = "roms/scsi/buslogic/BT-445C_AutoSCSI.rom";
            autoscsi_rom_size = 0x4000;
            has_scam_rom      = 0;
            dev->fw_rev       = "AA425J";
            dev->flags |= X54X_32BIT;
            dev->ha_bps = 10000000.0; /* fast SCSI */
            dev->max_id = 7;          /* narrow SCSI */
            break;
        case CHIP_BUSLOGIC_PCI_958D_1995_12_30: /*Dated December 30th, 1995*/
            strcpy(dev->name, "BT-958D");
            bios_rom_name     = "roms/scsi/buslogic/BT-958D_BIOS.rom";
            bios_rom_size     = 0x4000;
            bios_rom_mask     = 0x3fff;
            has_autoscsi_rom  = 1;
            autoscsi_rom_name = "roms/scsi/buslogic/BT-958D_AutoSCSI.rom";
            autoscsi_rom_size = 0x8000;
            has_scam_rom      = 1;
            scam_rom_name     = "roms/scsi/buslogic/BT-958D_SCAM.rom";
            scam_rom_size     = 0x0200;
            dev->fw_rev       = "AA507B";
            dev->flags |= (X54X_CDROM_BOOT | X54X_32BIT);
            dev->ha_bps = 20000000.0; /* ultra SCSI */
            dev->max_id = 15;         /* wide SCSI */
            break;

        default:
            break;
    }

    scsi_bus_set_speed(dev->bus, dev->ha_bps);

    if ((dev->Base != 0) && !(dev->card_bus & DEVICE_MCA) && !(dev->card_bus & DEVICE_PCI))
        x54x_io_set(dev, dev->Base, 4);

    memset(bl->AutoSCSIROM, 0xff, 32768);

    memset(bl->SCAMData, 0x00, 65536);

    if (bl->has_bios) {
        bl->bios_size = bios_rom_size;

        bl->bios_mask = 0xffffc000;

        rom_init(&bl->bios, bios_rom_name, bios_rom_addr, bios_rom_size, bios_rom_mask, 0, MEM_MAPPING_EXTERNAL);

        if (has_autoscsi_rom) {
            fp = rom_fopen(autoscsi_rom_name, "rb");
            if (fp) {
                (void) !fread(bl->AutoSCSIROM, 1, autoscsi_rom_size, fp);
                fclose(fp);
                fp = NULL;
            }
        }

        if (has_scam_rom) {
            fp = rom_fopen(scam_rom_name, "rb");
            if (fp) {
                (void) !fread(bl->SCAMData, 1, scam_rom_size, fp);
                fclose(fp);
                fp = NULL;
            }
        }
    } else {
        bl->bios_size = 0;

        bl->bios_mask = 0;
    }

    if (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30) {
        pci_add_card(PCI_ADD_NORMAL, BuslogicPCIRead, BuslogicPCIWrite, dev, &dev->pci_slot);

        buslogic_pci_bar[0].addr_regs[0] = 1;
        buslogic_pci_bar[1].addr_regs[0] = 0;
        buslogic_pci_regs[0x04]          = 3;

        /* Enable our BIOS space in PCI, if needed. */
        if (bl->has_bios) {
            buslogic_pci_bar[2].addr = 0xFFFFC000;
        } else {
            buslogic_pci_bar[2].addr = 0;
        }

        x54x_mem_init(dev, 0xfffd0000);
        x54x_mem_disable(dev);
    }

    if ((bl->chip == CHIP_BUSLOGIC_MCA_640A_1993_05_23) || (bl->chip == CHIP_BUSLOGIC_PCI_958D_1995_12_30))
        mem_mapping_disable(&bl->bios.mapping);

    buslogic_log("Buslogic on port 0x%04X\n", dev->Base);

    x54x_device_reset(dev);

    if ((bl->chip != CHIP_BUSLOGIC_ISA_542B_1991_12_14) && (bl->chip != CHIP_BUSLOGIC_ISA_545S_1992_10_05) && (bl->chip != CHIP_BUSLOGIC_ISA_542BH_1993_05_23) && (bl->chip != CHIP_BUSLOGIC_VLB_445S_1993_11_16) && (bl->chip != CHIP_BUSLOGIC_MCA_640A_1993_05_23)) {
        BuslogicInitializeLocalRAM(bl);
        BuslogicInitializeAutoSCSIRam(dev);
    }

    return dev;
}

// clang-format off
static const device_config_t BT_ISA_Config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x334,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x330", .value = 0x330 },
            { .description = "0x334", .value = 0x334 },
            { .description = "0x230", .value = 0x230 },
            { .description = "0x234", .value = 0x234 },
            { .description = "0x130", .value = 0x130 },
            { .description = "0x134", .value = 0x134 },
            { .description = "",      .value =     0 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 11,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 9",  .value =  9 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 12", .value = 12 },
            { .description = "IRQ 14", .value = 14 },
            { .description = "IRQ 15", .value = 15 },
            { .description = "", 0                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dma",
        .description    = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 6,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 5", .value = 5 },
            { .description = "DMA 6", .value = 6 },
            { .description = "DMA 7", .value = 7 },
            { .description = "",      .value = 0 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =       0 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "",         .value =       0 }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t BT958D_Config[] = {
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
};
// clang-format on

const device_t buslogic_542b_device = {
    .name          = "BusLogic BT-542B ISA",
    .internal_name = "bt542b",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CHIP_BUSLOGIC_ISA_542B_1991_12_14,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT_ISA_Config
};

const device_t buslogic_545s_device = {
    .name          = "BusLogic BT-545S ISA",
    .internal_name = "bt545s",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CHIP_BUSLOGIC_ISA_545S_1992_10_05,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT_ISA_Config
};

const device_t buslogic_542bh_device = {
    .name          = "BusLogic BT-542BH ISA",
    .internal_name = "bt542bh",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CHIP_BUSLOGIC_ISA_542BH_1993_05_23,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT_ISA_Config
};

const device_t buslogic_545c_device = {
    .name          = "BusLogic BT-545C ISA",
    .internal_name = "bt545c",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CHIP_BUSLOGIC_ISA_545C_1994_12_01,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT_ISA_Config
};

const device_t buslogic_640a_device = {
    .name          = "BusLogic BT-640A MCA",
    .internal_name = "bt640a",
    .flags         = DEVICE_MCA,
    .local         = CHIP_BUSLOGIC_MCA_640A_1993_05_23,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t buslogic_445s_device = {
    .name          = "BusLogic BT-445S VLB",
    .internal_name = "bt445s",
    .flags         = DEVICE_VLB,
    .local         = CHIP_BUSLOGIC_VLB_445S_1993_11_16,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT_ISA_Config
};

const device_t buslogic_445c_device = {
    .name          = "BusLogic BT-445C VLB",
    .internal_name = "bt445c",
    .flags         = DEVICE_VLB,
    .local         = CHIP_BUSLOGIC_VLB_445C_1994_12_01,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT_ISA_Config
};

const device_t buslogic_958d_pci_device = {
    .name          = "BusLogic BT-958D PCI",
    .internal_name = "bt958d",
    .flags         = DEVICE_PCI,
    .local         = CHIP_BUSLOGIC_PCI_958D_1995_12_30,
    .init          = buslogic_init,
    .close         = x54x_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = BT958D_Config
};
